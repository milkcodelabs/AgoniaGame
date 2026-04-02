#include <cstddef>

namespace stdext { 
    class exception; 
}

namespace std {
    // Keep this - it is missing in VS 2022
    void(__cdecl* _Raise_handler)(class stdext::exception const&) = nullptr;

    // Comment out or remove the locale/ctype/codecvt stuff for now!
    // The linker just told us it found them elsewhere.
}