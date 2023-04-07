/*
 * substring.h
 *
 *  Created on: 7. 4. 2023
 *      Author: ondra
 */

#ifndef SRC_COROSERVER_SUBSTRING_H_
#define SRC_COROSERVER_SUBSTRING_H_

#include <string>
#include <cassert>

namespace coroserver {


///Performs Knuth-Pratt-Morris algorithm to search for a substring in a string
/**
 * @tparam T type of item, default is char
 *
 * To use this class, construct an object with pattern. Then call append for every string
 * fragment while function returns false. When function returns true, pattern was found.
 *
 * You can use other functions to determine, where the pattern was found.
 *
 *
 */
template<typename T = char>
class SubstringSearch {
public:

    ///Construct the object
    SubstringSearch(std::basic_string_view<T> substr):_pattern(substr) {
        if (_pattern.empty()) throw std::invalid_argument("Empty pattern");
        if (_pattern.length()>255)  throw std::invalid_argument("Maximum allowed length of the pattern is 255 characters");
        assert("Max allowed pattern is 255 character long" && _pattern.length() < 256);
        _lps.resize(_pattern.length(),0);
        unsigned char len = 0;
        _lps[0] = 0;
        unsigned char i = 1;
        while (i < _pattern.length()) {
           if (_pattern[i] == _pattern[len]) {
               len++;
               _lps[i] = len;
               i++;
           } else {
               if (len != 0) {
                   len = _lps[len - 1];
               } else {
                   _lps[i] = 0;
                   i++;
               }
           }
        }
    }

    ///Append new text fragment
    /**
     * Note the string is not stored in the class. It is possible to pass infinite string
     *
     * @param text text fragment
     * @retval true pattern was found. Stop appending fragments
     * @retval false pattern was not found. Appened additional fragment
     *
     */
    bool append(std::string_view text) {
        std::string_view::size_type len = text.length();
        std::string_view::size_type i = 0;

        while (i < len) {
           if (text[i] == _pattern[_j]) {
               i++;
               _j++;
           }
           if (static_cast<std::size_t>(_j) == _pattern.length()) {
               _found_index = i - _j;
               return true;
           } else if (i < len && _pattern[_j] != text[i]) {
               if (_j != 0) {
                   _j = _lps[_j - 1];
               } else {
                   i++;
               }
           }
       }
       _fragment_index += len;
       return false;
    }


    ///Return global position of the pattern
    long get_global_pos() const {
        return _fragment_index + _found_index;
    }
    ///Return position of the pattern in last fragment
    /**
     * @return position of pattern relative to last fragment
     * @note function can return a negative number when the fragment started on
     * previous fragment
     */
    long get_fragment_pos() const {
        return _found_index;
    }
    ///Returns position of first character after found pattern in last fragment
    long get_pos_after_pattern() const {
        return _found_index+_pattern.length();
    }
    ///clear internal state, and prepare to next searcrh
    void clear() {
        _j = 0;
        _fragment_index = 0;
        _found_index = 0;
    }

protected:
    std::basic_string<T> _pattern;
    std::basic_string<unsigned char> _lps;
    long _fragment_index = 0;
    long _found_index = 0;
    long int _j = 0;
};

}


#endif /* SRC_COROSERVER_SUBSTRING_H_ */
