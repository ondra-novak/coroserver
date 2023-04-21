/*
 * urlencdec.h
 *
 *  Created on: 4. 11. 2022
 *      Author: ondra
 */

#ifndef SRC_USERVER_URLENCDEC_H_
#define SRC_USERVER_URLENCDEC_H_

#include <charconv>
#include <string>

namespace userver {

namespace url {

    template<typename Fn>
    inline void decode(const std::string_view &str, Fn &&fn) {
        for (auto iter = str.begin(); iter != str.end(); ++iter) {
            if (*iter == '%') {
                ++iter;
                if (iter != str.end()) {
                    char c[2];
                    c[0] = *iter;
                    ++iter;
                    if (iter != str.end()) {
                        c[1] = *iter;
                        int z = 32;
                        std::from_chars(c, c+2, z, 16);
                        fn(static_cast<char>(z));
                    } else {
                        break;
                    }
                } else {
                    break;
                }
            } else if (*iter == '+') {
                fn(' ');
            } else {
                fn(*iter);
            }
        }
    }
    
    template<typename Fn>
    inline void encode(const std::string_view &str, Fn &&fn) {
        for (char c : str) {
            if (std::isalnum(c) || c ==  '-' || c == '.' || c == '_' || c ==  '~' ) {
                fn(c);
            } else {
                fn('%');
                unsigned char x = c;
                auto c1 = x >> 4;
                auto c2 = x & 0xF;
                fn(c1>9?'A'+c1-10:'0'+c1);
                fn(c2>9?'A'+c2-10:'0'+c2);
            }
        }
    }
}

}



#endif /* SRC_USERVER_URLENCDEC_H_ */
