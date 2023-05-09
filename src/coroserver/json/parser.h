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

#if 0
struct Parser {

    class Stack;


    struct StateCompare {
        static constexpr bool repeat_last_char = false;
        std::string_view _text;
        Value _result;
        std::size_t _pos = 0;
        void operator()(Value &) {}
        bool operator()(char c, Stack &, Value &result) {
            if (c == _text[_pos]) {
                ++_pos;
                if (_pos != _text.length()) return false;
                result = std::move(_result);
                return true;
            } else {
                throw std::runtime_error("Parse error");
            }
        }
    };

    static int hexNumber(char c) {
        int out = (c >= 0 && c<='9')?c-'0':
                              (c >= 'A' && c <= 'F')?c-'A'+10:
                              (c >= 'a' && c <= 'f')?c-'a'+10: -1;
        if (out < 0) [[unlikely]] throw std::runtime_error("Invalid unicode escape sequence");
        return out;
    }

    static bool utf8_encode(unsigned int code_point, std::string &result) {
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

    struct StateString {
        static constexpr bool repeat_last_char = false;
        std::string _s;
        int _state = 0;
        unsigned int _unicode = 0;
        unsigned int _unicode2 = 0;
        void operator()(Value &) {}
        bool operator()(char c, Stack &, Value &result) {
            int h;
            switch (_state) {
                default:
                case 0: if (c == '"') {
                            result = std::move(_s);
                            return true;
                        } else if (c != '\\'){
                            _s.push_back(c);
                            return false;
                        } else {
                            _state = 1;
                            return false;
                        }
                case 1: _state = 0;
                         switch (c)  {
                            default: break;
                            case 'n':c = '\n';break;
                            case 'r':c = '\r';break;
                            case 't':c = '\r';break;
                            case 'f':c = '\t';break;
                            case 'b':c = '\b';break;
                            case 'u':{
                                _state = 2;
                                return false;
                            }
                        }
                        _s.push_back(c);
                        return false;
                case 2:
                case 3:
                case 4: _unicode = _unicode * 16 + hexNumber(c);
                        ++_state;
                        return false;
                case 5: _unicode = _unicode * 16 + hexNumber(c);
                        if ((_unicode & 0x7FF) == 0xD800) {
                            ++_state;
                        } else {
                            utf8_encode(_unicode, _s);
                            _unicode = 0;
                        }
                        return false;
                case 6: if (c != '\\') throw std::runtime_error("Expected '\\'");
                        ++_state;
                        return false;
                case 7: if (c != 'u') throw std::runtime_error("Expected 'u'");
                        ++_state;
                        return false;
                case 8:
                case 9:
                case 10:_unicode2 = _unicode2 * 16 + hexNumber(c);
                        ++_state;
                        return false;
                case 11:_unicode2 = _unicode2 * 16 + hexNumber(c);
                        if ((_unicode2 & 0x7FF) == 0xD800) {
                            //first is high surrogate, other is low surrogate
                            if (_unicode2 < _unicode) std::swap(_unicode2, _unicode);
                            int u  = ((_unicode & ~0x3FF) << 10) | (_unicode2 & ~0x3FF);
                            utf8_encode(u, _s);
                            _unicode = _unicode2 = 0;
                        } else {

                            throw std::runtime_error("Expected surrogate pair");
                        }
                        _state = 0;
                        return false;
            }
        }
    };

    struct StateNumber {
        static constexpr bool repeat_last_char = true;
        std::string _s;
        int _state = 0;

        static void error() {
            throw std::runtime_error("Invalid number format");
        }
        void operator()(Value &) {}
        bool operator()(char c, Stack &, Value &result) {
            _s.push_back(c);
            while (true)

                //0 READSIGN
                //1 EXPECT 1 NUMBER
                //2 READNUMBERS
                //3 READ DOT OR E OR EOF -> exit
                //4 EXPECT 1 NUMBER
                //5 READNUMBERS
                //6 READ E OR EOF
                //7 READSIGN
                //8 EXPECT 1 NUMBER
                //9 READNUMBERS
                //10 FINALIZE

                switch (_state) {
                            // FINALIZE
                    default: result = TextNumber(_s)    ;
                             return true;



                            //READSIGN
                    case 7:
                    case 0: _state++;
                            if (c == '+' || c == '-') return false;
                            if (c < '0' || c > '9') error();
                            break;

                            //READNUMBERS
                    case 2:
                    case 5:
                    case 9: if (c >= '0' && c <='9') return false;
                            _state++;
                            break;


                            //READ DOT OR E OR EOF
                    case 3: if (c == '.') {
                                _state++;
                                return false;
                            } else if (c == 'e' || c == 'E') {
                                _state=7;
                                return false;
                            }
                            _state = 10;
                            break;
                            //EXPECT 1 NUMBER
                    case 1:
                    case 4:
                    case 8: if (c >= '0' && c <='9') {
                                _state++;
                                return false;
                            }
                            error();
                            break;
                            //READ E OR EOF
                    case 6: if (c == 'e' || c == 'E') {
                                _state++;
                                return false;
                            }
                            _state = 10;
                            break;

                }
        }
    };

    struct StateArray {
        static constexpr bool repeat_last_char = false;
        Array _arr;
        int _state = 0;
        void operator()(Value &return_value) {
            _arr.push_back(std::move(return_value));
            ++_state;
        }
        bool operator()(char c, Stack &st, Value &result) {
            switch (_state) {
                case 0: if (c != '[') throw std::runtime_error("Expected '['");
                        ++_state;
                        return false;
                case 1: if (std::isspace(c)) return false;
                        st.push(State(c), c, result);
                        return false;
            }
        }

    };
    struct StateObject {
        static constexpr bool repeat_last_char = false;
        Object _arr;
        int _state = 0;
        bool operator()(char c, Stack &, Value &result) {

        }
    };


    using StateVariant = std::variant<StateCompare, StateString, StateNumber>;

    class State: public StateVariant {
    public:
        using StateVariant::StateVariant;
        State(char c): StateVariant(initState(c)){}

        static StateVariant initState(char c) {
            switch (c) {
                case '{': return StateObject{};
                case '[': return StateArray{};
                case '"': return StateString{};
                case '+':
                case '-': return StateNumber{};
                case 'n': return StateCompare{"null",nullptr};
                case 't': return StateCompare{"true",true};
                case 'f': return StateCompare{"false",false};
            }
        }
    };

    class Stack : public std::stack<State> {
    public:

        return push(State &&state, char c, Value &result) {
            std::stack<State>::push(std::move(state));;
            return execute(c, result);
        }
        bool execute(char c, Value &result) {
            bool v  = std::visit([&](auto &x){
                return x(c, *this, result);
            },top());
            if (v) {
                pop();
                if (empty()) return true;
                else {
                    bool rep =std::visit([&](auto &x){
                        x(result);
                        return x.repeat_last_char;
                    }, top());
                    if (rep) return execute(c, result);
                }
            } else {
                return false;
            }
        }
    };

public:

    bool operator()(char c);


    operator Value &&() const {

    }


protected:
};


#endif


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
