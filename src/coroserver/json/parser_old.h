#pragma once
#ifndef SRC_USERVER_JSON_PARSER_H_
#define SRC_USERVER_JSON_PARSER_H_
#include "value.h"

#include <stack>

namespace userver {
namespace json {

class Parser {
protected:

    enum State {
        result,
        node,
        object,
        array,
        string,
        string_esc,
        key,
        key_save,
        dcol,
        token,
        number
    };

    struct Level {
        State state;
        Value val;

    };

    std::stack<Level> _levels;
    bool _is_error = false;

    std::vector<char> _buffer;
    std::string_view::const_iterator _begcmp = {};
    std::string_view::const_iterator _endcmp = {};
    std::string _curkey;



    void push_back(char c) {
        Level &curlev = _levels.top();
        switch (curlev.state) {
            case node:
                switch (c) {
                    case '{':
                        curlev.state = object;
                        curlev.val = Object();
                        _levels.push({key});
                        break;
                    case '[':
                        curlev.state = array;
                        curlev.val = Array();
                        _levels.push({node});
                        break;
                    case '}':
                        _levels.pop();
                        finish_object();
                        break;
                    case ']':
                        _levels.pop();
                        finish_array();
                        break;
                    case '"':
                        _buffer.clear();
                        curlev.state = string;
                        break;
                    case 't':
                        _begcmp = str_true.begin();
                        _endcmp = str_true.end();
                        ++_begcmp;
                        curlev.val = true;
                        curlev.state = token;
                        break;
                    case 'f':
                        _begcmp = str_false.begin();
                        _endcmp = str_false.end();
                        ++_begcmp;
                        curlev.val = false;
                        curlev.state = token;
                        break;
                    case 'n':
                        _begcmp = str_null.begin();
                        _endcmp = str_null.end();
                        ++_begcmp;
                        curlev.val = nullptr;
                        curlev.state = token;
                        break;
                    case '+':
                    case '-':
                    case '0':
                    case '1':
                    case '2':
                    case '3':
                    case '4':
                    case '5':
                    case '6':
                    case '7':
                    case '8':
                    case '9':
                        _buffer.clear();
                        _buffer.push_back(c);
                        curlev.state = number;
                        break;
                    default:
                        if (!isspace(c)) {
                            _is_error = true;
                        }
                        break;
                }
                break;

            case token:
                if (c != *_begcmp) {
                    _is_error = true;
                } else {
                    ++_begcmp;
                    if (_begcmp == _endcmp) {
                        finish_item();
                    }
                }
                break;
            case number:
                if (std::isdigit(c) || c == 'e' || c == 'E' || c == '+' || c == '-' || c == '.') {
                    _buffer.push_back(c);
                } else {
                    if (!check_number()) {
                        _is_error = true;
                        break;
                    } else {
                        curlev.val = TextNumber(std::string(_buffer.data(), _buffer.size()));
                        finish_item();
                        push_back(c);
                    }
                }
                break;
            case string:
                switch (c) {
                    case '"':
                        curlev.val = buffer_to_string();
                        finish_item();
                        break;
                    case '\\':
                        curlev.state = string_esc;
                        [[fallthrough]];
                    default:
                        _buffer.push_back(c);
                        break;
                }
                break;
            case string_esc:
                _buffer.push_back(c);
                curlev.state = string;
                break;
            case dcol:
                if (c != ':')  {
                    _is_error = !std::isspace(c);
                } else {
                    _levels.pop();
                }
                break;
            case key:
                if (c != '"') {
                    _is_error = !std::isspace(c);
                } else {
                    _buffer.clear();
                    _levels.push({string});
                }
                break;
            case object:
                switch (c) {
                    case '}':
                        finish_object();
                        break;
                    case ',':
                        _levels.push({key});
                        break;
                    default:
                        _is_error = !std::isspace(c);
                        break;
                }
                break;
            case array:
                switch (c) {
                    case ']':
                        finish_array();
                        break;
                    case ',':
                        _levels.push({node});
                        break;
                    default:
                        _is_error = !std::isspace(c);
                        break;
                }
                break;
            case key_save:
            case result:
                _is_error = true;
                break;
        }
    }

    void finish_object() {
        if (_levels.top().state != object) {
            _is_error = true;
        } else {
            finish_item();
        }
    }
    void finish_array() {
        if (_levels.top().state != array) {
            _is_error = true;
        } else {
            finish_item();
        }
    }

    void finish_item() {
        Value v(std::move(_levels.top().val));
        _levels.pop();
        switch (_levels.top().state) {
            case object: {
                _is_error = true;
            }break;
            case array:
                _levels.top().val.array().push_back(std::move(v));
                break;
            case key: {
                _levels.pop();
                _levels.push({key_save, v});
                _levels.push({node});
                _levels.push({dcol});
                }break;
            case key_save: {
                std::string key = _levels.top().val.get_string();
                _levels.pop();
                auto r = _levels.top().val.object().emplace(std::move(key), std::move(v));
                _is_error = !r.second;
            } break;
            case result:
                _levels.top().val = std::move(v);
                break;
            default:
                _is_error = true;
                break;
        }
    }

    void push_eof() {
        Level &curlev = _levels.top();
        if (curlev.state == number) {
            if (check_number()) {
                curlev.val = TextNumber(std::string(_buffer.data(), _buffer.size()));
                finish_item();
                return ;
            }
        }
        _is_error = true;

    }


    std::string buffer_to_string() {
        std::string s;
        s.reserve(_buffer.size());
        _buffer.push_back('\0'); //character zero is terminator (zero is not allowed)
        auto iter = _buffer.begin();
        while (*iter) {
            if (*iter == '\\') {
                ++iter;
                switch(*iter) {
                    case '"':
                    case '\\':
                    case '/':s.push_back(*iter);
                             break;
                    case 'n':s.push_back('\n');
                             break;
                    case 'r':s.push_back('\r');
                             break;
                    case 't':s.push_back('\t');
                             break;
                    case 'f':s.push_back('\f');
                             break;
                    case 'b':s.push_back('\b');
                             break;
                    case 'u':{
                        ++iter;
                        unsigned int unicode = read_hex_str(iter);  //read hex XXXX
                        if (_is_error) return {};
                        if ((unicode & ~0x7FF) == 0xD800) { //this is surrogate pair
                            if (*iter != '\\') {        // new escape must follow
                                _is_error = true; return {};
                            }
                            ++iter;
                            if (*iter != 'u') {         // u must follow
                                _is_error = true; return {};
                            }
                            ++iter;
                            unsigned int unicode2 = read_hex_str(iter); //read next XXX
                            //first is high surrogate, other is low surrogate
                            if (unicode2 < unicode) std::swap(unicode2, unicode);
                            //build codepoint
                            unicode  = ((unicode & ~0x3FF) << 10) | (unicode2 & ~0x3FF);
                        }
                        //convert unicode codepoint to utf-8 sequence
                        if (!utf8_encode(unicode, s)) {
                            _is_error = true;
                            return {};
                        }
                        break;
                    }break;
                    default:
                        _is_error = true;
                        return {};
                }
            } else {
                s.push_back(*iter);
                ++iter;
            }
        }
        return s;
    }

    bool check_number() {
        _buffer.push_back('\0');    //add terminator
        auto iter = _buffer.begin();
        if (*iter == '+' || *iter == '-') ++iter; //+ or -
        if (!std::isdigit(*iter)) return false; //required digit
        ++iter;
        while (std::isdigit(*iter)) ++iter; //more digits
        if (*iter == '.') { //if dot follows
            ++iter;
            if (!std::isdigit(*iter)) return false; //required digit
            ++iter;
            while (std::isdigit(*iter)) ++iter;  //more digits
        }
        if ((*iter == 'e' || *iter == 'E')) { //e follows ?
            ++iter;
            if (*iter == '+' || *iter == '-') ++iter; //+ or - is optional
            if (!std::isdigit(*iter)) return false; //required digit
            ++iter;
            while (std::isdigit(*iter)) ++iter;  //more digits
        }
        if (*iter != '\0') return false;    //must be terminator here
        _buffer.pop_back(); //remove terminator
        return true;
    }

    template<typename Iter>
    unsigned int read_hex_str(Iter &iter) {
        unsigned int unicode = 0;
        for (int i = 0; i<4; i++) {
            char c= *iter;
            ++iter;
            unicode = unicode << 4;
            if (std::isdigit(c)) unicode += c - '0';
            else if (c >= 'A' && c <= 'F') unicode += c - 'A' + 10;
            else if (c >= 'a' && c <= 'f') unicode += c - 'a' + 10;
            else {
                _is_error = true;
                return 0;
            }
        }
        return unicode;

    }

    bool utf8_encode(unsigned int code_point, std::string &result) {
        if (code_point <= 0x7F) {
            result.push_back(static_cast<char>(code_point));
        } else if (code_point <= 0x7FF) {
            result.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
            result.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        } else if (code_point <= 0xFFFF) {
            result.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
            result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        } else if (code_point <= 0x10FFFF) {
            result.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
            result.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        } else {
            return false;
        }
        return true;
    }



public:

    Parser() {
        _levels.push({result});
        _levels.push({node});
    }

    std::string_view parse_string(std::string_view str) {
        if (str.empty()) {
            push_eof();
            return str;
        }
        std::size_t sz = str.size();
        for (std::size_t i = 0; i < sz; ++i) {
            push_back(str[i]);
            if (!more()) {
                return str.substr(i+1);
            }
        }
        return {};
    }

    bool more() const {
        return !_is_error && _levels.size()>1;
    }

    bool is_error() const {
        return _is_error;
    }

    Value &get_result() {
        return _levels.top().val;
    }
    const Value &get_result() const {
        return _levels.top().val;
    }

protected:

};

}
}



#endif /* SRC_USERVER_JSON_PARSER_H_ */
