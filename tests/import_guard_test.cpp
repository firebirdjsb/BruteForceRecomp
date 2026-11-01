#include "import_guard.h"
#include <cstdio>

int main() {
    struct Case { uint32_t word; bool accepted; };
    const Case cases[] = {
        {0, false}, {0x7fffffff, false}, {0x80000000, false},
        {0x80000001, true}, {0x80000031, true}, {0x8000017a, true},
        {0x8000017b, false}, {0x8000ffff, false},
        {0xc3c3c3c3, false}, {0x8b5e5d00, false}, {0xffffffff, false}
    };
    for (const auto &test : cases) {
        if (bool(bf_valid_extra_kernel_thunk(test.word)) != test.accepted) {
            std::fprintf(stderr, "Import guard failed for 0x%08X\n", test.word);
            return 1;
        }
    }
    std::printf("PASS: 11 valid, invalid and boundary import-marker cases\n");
    return 0;
}
