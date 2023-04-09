#ifndef SRC_COROSERVER_JSON_PARSER_H_
#define SRC_COROSERVER_JSON_PARSER_H_

#include <cocls/future.h>
#include <cocls/generator.h>
#include "../character_io.h"
#include "value.h"
#include <utility>

#include <stack>

namespace coroserver{

namespace json {




template<typename Alloc, typename Stream>
inline cocls::with_allocator<Alloc, cocls::async<Value> > parse_coro(Alloc &, Stream stream) {
    std::stack<Value> stack;
    CharacterReader rd(stream);
    std::string buffer;


    //check, whether buffer contains valid number
    auto check_number = [](std::string &buffer){
        buffer.push_back('\0');    //add terminator
        auto iter = buffer.begin();
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
        buffer.pop_back(); //remove terminator
        return true;
    };

    //converts text in buffer to a valid utf-8 string
    auto buffer_to_string = [](std::string &buffer) -> std::string {

        auto read_hex_str = [](auto &iter) {
              int unicode = 0;
              for (int i = 0; i<4; i++) {
                  char c= *iter;
                  ++iter;
                  unicode = unicode << 4;
                  if (std::isdigit(c)) unicode += c - '0';
                  else if (c >= 'A' && c <= 'F') unicode += c - 'A' + 10;
                  else if (c >= 'a' && c <= 'f') unicode += c - 'a' + 10;
                  else return -1;
              }
              return unicode;
          };

          auto utf8_encode = [] (unsigned int code_point, std::string &result) {
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
          };

        std::string s;
        s.reserve(buffer.size());
        buffer.push_back('\0'); //character zero is terminator (zero is not allowed)
        auto iter = buffer.begin();
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
                        int unicode = read_hex_str(iter);  //read hex XXXX
                        if (unicode < -1) throw std::runtime_error("Invalid unicode sequence");
                        if ((unicode & ~0x7FF) == 0xD800) { //this is surrogate pair
                            if (*iter != '\\') {        // new escape must follow
                                throw std::runtime_error("Expected surrogate pair");
                            }
                            ++iter;
                            if (*iter != 'u') {         // u must follow
                                throw std::runtime_error("Expected surrogate pair");
                            }
                            ++iter;
                            int unicode2 = read_hex_str(iter); //read next XXX
                            //first is high surrogate, other is low surrogate
                            if (unicode2 < unicode) std::swap(unicode2, unicode);
                            //build codepoint
                            unicode  = ((unicode & ~0x3FF) << 10) | (unicode2 & ~0x3FF);
                        }
                        //convert unicode codepoint to utf-8 sequence
                        if (!utf8_encode(unicode, s)) {
                            throw std::runtime_error("Invalid unicode sequence");
                        }
                        break;
                    }break;
                    default:
                        throw std::runtime_error("Invalid escape sequence");

                }
            } else {
                s.push_back(*iter);
                ++iter;
            }
        }
        return s;
    };

    auto is_space = [](int i){return i > 0 && i < 33;};

    //contains currently built object
    Value cobj;
    //if check is nonempty, we need to check and compare following character sequence
    std::string_view check;

    do {
        //read character
        int c;
        do  c = co_await rd; while (is_space(c));
        //depend on character
        switch (c) {
            //read string
            case '"': {
                //clear buffer
                buffer.clear();
                //read next char
                c = co_await rd;
                //read until '"' or EOF is found
                while (c != '"' && c != -1) {
                   //push characters
                   buffer.push_back(c);
                   //if escape found
                   if (c == '\\') {
                       //read extra character
                       c = co_await rd;
                       //and directly push it, if it is not EOF
                       if (c != -1) buffer.push_back(c);
                       //in case of EOF< break
                       else break;
                   }
                   //read next character
                   c = co_await rd;
                }
                //check, whether '"' was read
                if (c != '"') throw std::runtime_error("Unexpected EOF reading string");
                //convert buffer to valid string
                cobj = buffer_to_string(buffer);
            }break;
            //read number
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
            case '9':{
                //clear buffer
                buffer.clear();
                //push first character
                buffer.push_back(c);

                c = co_await rd;
                //Reading of number ends by comma, end of array or end of object
                while (c != ',' && c != '}' && c != ']' && c != -1) {
                    //push character to buffer
                    buffer.push_back(c);
                    //read next
                    c = co_await rd;
                }
                //if not eof, put character back, we will need character later
                if (c != -1) rd.put_back();
                //check whether this was really number
                if (!check_number(buffer)) {
                    throw std::runtime_error("Invalid number format");
                }
                //store as TextNumber
                cobj = TextNumber(buffer);
            }break;
            //read object
            case '{':
                //just push Object
                stack.push(Object());
                continue;
            //read arra
            case '[':
                //just push Arrat
                stack.push(Array());
                continue;
            //read true
            case 't':
                check = "rue";  //check (t)rue
                cobj = true;    //expect success - true
                break;
            case 'f':
                check = "alse"; //check (f)alse
                cobj = false;  //expect success - false
                break;
            case 'n':
                check = "ull"; //expect success - null
                cobj = nullptr; //check (n)ull
                break;
            case -1:
                //eof
                throw std::runtime_error("Unexpected EOF");
            default:
                //unknown character
                throw std::runtime_error("Unexpected character");
        }

        //so, if check requested, check now
        if (!check.empty()) {
            //check all characters
            for (int c: check) {
                bool b = c == co_await rd;
                //report error if not same
                if (!b) throw std::runtime_error("Unknown keyword");
            }
            check = {};
        }

        //now we will reduce stack
        //set cycle to false to stop reducing
        bool cycle = true;
        while (cycle && !stack.empty()) {
            //pick top object
            Value &v = stack.top();
            //switch by its type
            switch (v.type()) {
                //if string is on top, is is key, and cobj contains value
                //there should be object below
                case Type::string: {
                    //pick key
                    Value key (std::move(v));
                    //retrieve object
                    stack.pop();
                    Value &w = stack.top();
                    //emplace key-value pair
                    if (!w.object().emplace(key.get_string(), std::move(cobj)).second) {
                        //we was unable to insert the key, report duplication
                        throw std::runtime_error("Duplicate keys in JSON");
                    }
                    //read next char, expected ',' or '}'
                    do  c = co_await rd; while (is_space(c));
                    //if c is '}' - end of object
                    if (c == '}') {
                        //put top object to cobj
                        cobj = std::move(w);
                        //pop it
                        stack.pop();
                        //we continue in cycle

                    } else if (c == ',') {
                        //in case of comma, no reduction, break cycle
                        cycle = false;
                    } else {
                        //otherwise error
                        throw std::runtime_error("Unexpected character, expected ',' or '}'");
                    }
                }break;
                //if array is on the top, cobj is appended
                case Type::array: {
                    //just push back the cobj
                    v.array().push_back(std::move(cobj));
                    //read next character
                    do  c = co_await rd; while (is_space(c));
                    //we expect ']' or ','
                    if (c == ']') {
                        //when ']', make cobj from top
                        cobj = std::move(v);
                        //pop from stack
                        stack.pop();
                        //continue in cycle

                    } else if (c == ',') {
                        //for comme break cycle
                        cycle = false;
                    } else {
                        //othewise error
                        throw std::runtime_error("Unexpected character, expected ',' or ']'");
                    }

                }break;
                //if object is on top, cobj must be a string, which is used as a key
                case Type::object: {
                    //check, if the cobj is string
                    if (cobj.type() != Type::string) {
                        throw std::runtime_error("Expected key (string)");
                    }
                    //read next character
                    do  c = co_await rd; while (is_space(c));
                    //there must be only ':'
                    if (c != ':') {
                        //otherwise error
                        throw std::runtime_error("Expected ':' after key");
                    }
                    //push key to stack
                    stack.push(std::move(cobj));
                    //break cycle
                    cycle = false;
                }break;
                default:
                    //any other state is invalid
                    throw std::runtime_error("Invalid state");
            }
        }

    } while (!stack.empty());

    co_return std::move(cobj);
}


template<typename Stream>
cocls::future<Value> parse(Stream stream) {
    cocls::default_storage stor;
    return parse_coro(stor, std::move(stream));

}




}

}



#endif /* SRC_COROSERVER_JSON_PARSER_H_ */
