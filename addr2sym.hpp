#pragma once

#include <string>
#include <vector>

#if __unix__
# include <cxxabi.h>
# include <execinfo.h>

inline std::string addr2sym(void *addr) {
    if (addr == nullptr) {
        return "null";
    }
    char **strings = backtrace_symbols(&addr, 1);
    if (strings == nullptr) {
        return "???";
    }
    std::string ret = strings[0];
    free(strings);
    auto pos = ret.find('(');
    if (pos != std::string::npos) {
        auto pos2 = ret.find('+', pos);
        if (pos2 != std::string::npos) {
            auto pos3 = ret.find(')', pos2);
            auto offset = ret.substr(pos2, pos3 - pos2);
            if (pos2 != pos + 1) {
                ret = ret.substr(pos + 1, pos2 - pos - 1);
                char *demangled =
                    abi::__cxa_demangle(ret.data(), nullptr, nullptr, nullptr);
                if (demangled) {
                    ret = demangled;
                    free(demangled);
                } else {
                    ret += "()";
                    ret += offset;
                }
            } else {
                ret = ret.substr(0, pos) + offset;
                auto slash = ret.rfind('/');
                if (slash != std::string::npos) {
                    ret = ret.substr(slash + 1);
                }
            }
        }
    }
    return ret;
}
#elif _MSC_VER
# include <windows.h>
# include <dbghelp.h>
# include <stdio.h>

# pragma comment(lib, "dbghelp.lib")

extern "C" char *__unDName(char *, char const *, int, void *, void *, int);

inline std::string addr2sym(void *addr) {
    if (addr == nullptr) {
        return "null";
    }
    DWORD64 dwDisplacement;
    char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
    pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    pSymbol->MaxNameLen = MAX_SYM_NAME;
    if (!SymFromAddr(GetCurrentProcess(), (DWORD64)addr, &dwDisplacement,
                     pSymbol)) {
        char buf[64];
        snprintf(buf, sizeof(buf), "0x%8llx", (DWORD64)addr);
        return buf;
    }
    std::string name(pSymbol->Name);
    // demangle
    char undname[1024];
    __unDName(undname, name.c_str() + 1, sizeof(undname), malloc, free, 0x2800);
    name = undname;
    return name;
}
#elif _WIN32
# include <windows.h>
# include <dbghelp.h>

# pragma comment(lib, "dbghelp.lib")

inline std::string addr2sym(void *addr) {
    DWORD64 dwDisplacement;
    char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
    pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    pSymbol->MaxNameLen = MAX_SYM_NAME;
    if (!SymFromAddr(GetCurrentProcess(), (DWORD64)addr, &dwDisplacement,
                     pSymbol)) {
        return "???";
    }
    std::string name(pSymbol->Name);
    // demangle
    if (name.find("??") == 0) {
        char undname[1024];
        if (UnDecorateSymbolName(pSymbol->Name, undname, sizeof(undname),
                                 UNDNAME_COMPLETE)) {
            name = undname;
        }
    }
    return name;
}
#else
# include <sstream>

inline std::string addr2sym(void *addr) {
    if (addr == nullptr) {
        return "null";
    }
    std::ostringstream oss;
    oss << "0x" << std::hex << addr;
    return oss.str();
}
#endif

inline std::vector<std::string> addrList2symList(std::vector<void*> addrList) {
    std::vector<std::string> symList;
    for(auto addr : addrList) {
        symList.push_back(addr2sym(addr));
    }
    return symList;
}
