#include "win32error.h"
#include <chrono>

namespace coroserver {

class IOCP {
public:

    IOCP() {
        _h = CreateIoCompletionPort(NULL,NULL,0,0);
        if (!_h) throw Win32Error("IOCP creation");
    }
    ~IOCP() {
        if (_h != INVALID_HANDLE_VALUE) CloseHandle(_h);
    }

    IOCP(IOCP &&other):_h(other._h) {other._h = INVALID_HANDLE_VALUE;}
    IOCP &operator=(IOCP &&other) {
        if (this != &other) {
            if (_h != INVALID_HANDLE_VALUE) CloseHandle(_h);
            _h = other._h;
            other._h = INVALID_HANDLE_VALUE;
        }
        return *this;
    }

    void add(HANDLE h, ULONG_PTR key) {
        HANDLE x = CreateIoCompletionPort(h, _h, key, 0);
        if (!x) throw Win32Error("Associate handle with IOCP");
    }

    struct Event {
        DWORD bytes;
        LPOVERLAPPED overlapped;
        ULONG_PTR key;
        DWORD error;
    };

    Event wait(std::chrono::system_clock::time_point timeout) {
        Event out = {};
        DWORD tm = INFINITE;
        if (timeout < timeout.max())  {
            auto df = std::chrono::duration_cast<std::chrono::milliseconds>(timeout - std::chrono::system_clock::now()).count();
            if (df < 0) tm = 0;
            else if (df >= static_cast<decltype(df)>(INFINITE)) tm = INFINITE-1;
            else df = static_cast<DWORD>(df);
        }
        DWORD bytes;
        LPOVERLAPPED overlapped;
        ULONG_PTR key;
        DWORD error = 0;
        if (!GetQueuedCompletionStatus(_h, &bytes, &key, &overlapped, tm)) {
            error = GetLastError();
            if (overlapped == NULL) {
                if (error == ERROR_TIMEOUT) {
                    out.error = error;
                    return out;
                }
                else throw Win32Error(error, "GetQueuedCompletionStatus");
            } 
        }
        out.bytes = bytes;
        out.error = error;
        out.key = key;
        out.overlapped = overlapped;
        return out;
    }

    void post(ULONG_PTR key, DWORD bytes = 0, LPOVERLAPPED overlapped = NULL) {
        if (!PostQueuedCompletionStatus(_h, bytes,key,overlapped)) {
            throw Win32Error("PostQueuedCompletionStatus");
        }        
    }

protected:
    HANDLE _h;



};



}