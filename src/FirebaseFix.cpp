#include <cstddef>

namespace stdext { 
    class exception; 
}

namespace std {
    void(__cdecl* _Raise_handler)(class stdext::exception const&) = nullptr;
}