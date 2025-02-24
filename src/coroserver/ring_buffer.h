#pragma once
#include <vector>

///String ring buffer
template<typename T>
class StringRingBuffer {
public:

    ///current size of the ring buffer
    constexpr std::size_t size() const {
        if (_read_pos > _write_pos) {
            return _buffer.size() - _read_pos + _write_pos;
        } else {
            return _write_pos - _read_pos;
        }
    }

    ///current capacity of the ring buffer
    constexpr std::size_t capacity() const {
        return std::max<std::size_t>(1,_buffer.size()) - 1;
    }

    ///reserve capacity
    /**
     * @param new_capacity new capacity, it must be larger than current capacity
     */
    constexpr void reserve(std::size_t new_capacity) {
        new_capacity+=1;
        if (_buffer.size() < new_capacity) {
            auto cur_size = size();
            std::vector<T> new_buffer(new_capacity);
            if (_read_pos < _write_pos) {
                std::move(_buffer.begin()+_read_pos, _buffer.begin()+_write_pos, new_buffer.begin());
            } else if (_read_pos > _write_pos) {
                std::move(_buffer.begin(), _buffer.begin()+_write_pos,
                   std::move(_buffer.begin()+_read_pos, _buffer.end(), new_buffer.begin()));
            }
            _write_pos = cur_size;
            _read_pos = 0;
            std::swap(_buffer, new_buffer);
        }
    }

    ///push string to the buffer
    /**
     * @param data data to push
     */
    constexpr void push(std::basic_string_view<T> data) {
        while (!data.empty()) {
            reserve(size()+data.size()*2-1);
            auto remain = _buffer.size() - _write_pos;
            auto b1 = data.substr(0, remain);
            std::copy(b1.begin(), b1.end(),_buffer.begin()+_write_pos);
            _write_pos = _write_pos + b1.size();
            if (_write_pos >= _buffer.size()) {
                _write_pos -= _buffer.size();
            }
            data = data.substr(b1.size());
        }
    }

    ///Retrieve view to pushed data (or some of the data)
    /**
     * @return Returns a linear view of the data. Returned string can
     * be less than current size, you must retrieve data per-partes
     */
    constexpr std::basic_string_view<T> front() const {
        if (_read_pos < _write_pos) {
            return {_buffer.data()+_read_pos, _write_pos - _read_pos};
        } else if (_write_pos < _read_pos) {
            return {_buffer.data()+_read_pos, _buffer.size() - _read_pos};
        } else {
            return {};
        }
    }

    ///Pop retrieved data
    /**
     * @param sz count of items to remove from the buffer
     */
    constexpr void pop(std::size_t sz) {
        if (_read_pos < _write_pos) {
            _read_pos = std::min(_write_pos, _read_pos+sz);
        } else if (_read_pos > _write_pos) {
            auto s1 = std::min(sz, _buffer.size() - _read_pos);
            _read_pos += s1;
            if (_read_pos >= _buffer.size()) {
                _read_pos -= _buffer.size();
            }
            _read_pos += sz - s1;
        }
    }

    constexpr bool empty() const {
        return _read_pos == _write_pos;
    }

    constexpr void clear() {
        _buffer.clear();
        _read_pos = _write_pos = 0;
    }

protected:
    std::vector<T> _buffer;
    std::size_t _read_pos = 0;
    std::size_t _write_pos = 0;
};
