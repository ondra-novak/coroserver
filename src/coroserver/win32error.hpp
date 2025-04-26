#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <system_error>
#include <string>



class Win32ErrorCategory: public std::error_category {
public:

    static std::string GetErrorMessage(int _Errval) {
        wchar_t *s = NULL;
        FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, 
                NULL, static_cast<DWORD>(_Errval),
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                (LPWSTR)&s, 0, NULL);
    
        std::size_t sz = wcslen(s);
        std::size_t needsz = WideCharToMultiByte(CP_UTF8,0,s,static_cast<int>(sz),NULL,0,NULL,NULL);
        std::string out;
        out.resize(needsz);
        WideCharToMultiByte(CP_UTF8,0,s,static_cast<int>(sz),out.data(),static_cast<int>(out.size()),NULL,NULL);
        LocalFree(s);
        out.append("(Error code=").append(std::to_string(_Errval)).append(")");
        return out;
    }
    

    virtual ~Win32ErrorCategory() noexcept = default;
    virtual const char* name() const noexcept override {return "Win32 Error";}
    virtual std::string message(int _Errval) const override {return GetErrorMessage(_Errval);}
};

    
class Win32Error: public std::system_error {
public:
    Win32Error():std::system_error(static_cast<int>(GetLastError()), Win32ErrorCategory()) {}
    Win32Error(std::string message):std::system_error(static_cast<int>(GetLastError()), Win32ErrorCategory(), message) {}

    Win32Error(DWORD error):std::system_error(static_cast<int>(error), Win32ErrorCategory()) {}
    Win32Error(DWORD error, std::string message):std::system_error(static_cast<int>(error), Win32ErrorCategory(), message) {}
};
    