#include "platform_files.h"

// The path text rules every platform shares. The file calls themselves are
// in platform_files_win.cpp and platform_files_posix.cpp.
namespace platform {

bool IsSeparator(wchar_t c) {
    return c == L'\\' || c == L'/';
}

std::string Join(const std::string& folder, const std::string& name) {
    return folder + kSeparator + name;
}

std::wstring Join(const std::wstring& folder, const std::wstring& name) {
    return folder + kWideSeparator + name;
}

std::string NativeSeparators(std::string path) {
    for (char& c : path) {
        if (c == '\\' || c == '/') {
            c = kSeparator;
        }
    }
    return path;
}

std::wstring NativeSeparators(std::wstring path) {
    for (wchar_t& c : path) {
        if (IsSeparator(c)) {
            c = kWideSeparator;
        }
    }
    return path;
}

}  // namespace platform
