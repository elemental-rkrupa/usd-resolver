#include <windows.h>
#include <stdio.h>

typedef void* (*CreateFn)(void);

void TryInstantiate(CreateFn fn) {
    __try {
        void* r = fn();
        fprintf(stderr, "Instantiation succeeded: %p\n", r);
        fflush(stderr);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        fprintf(stderr, "CRASH! Exception code: 0x%08X\n", GetExceptionCode());
        fflush(stderr);
    }
}