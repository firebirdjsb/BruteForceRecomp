// Linux ucontext/register semantics for the reused public MMIO decoder.
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../third_party/xboxrecomp-mmio/mmio_decode.h"

typedef struct { uint64_t value, stored; unsigned reads, writes; int width; } Device;
static uint64_t read_register(void *state, uint32_t off, int size) {
    Device *device = state;
    if (off != 0x10) return 0;
    device->reads++;
    device->width = size;
    return device->value;
}
static void write_register(void *state, uint32_t off, uint64_t value, int size) {
    Device *device = state;
    if (off != 0x10) return;
    device->writes++;
    device->width = size;
    device->stored = value;
}
#define CHECK(expression) do { if (!(expression)) { \
    fprintf(stderr, "MMIO check failed at line %d: %s\n", __LINE__, #expression); return 1; \
} } while (0)

static int emulate(ucontext_t *context, Device *device, const uint8_t *code, size_t length) {
    MMIO_CTX_RIP(context) = (uintptr_t)code;
    int result = mmio_emulate_host(context, 0x10, device, read_register, write_register);
    return result && MMIO_CTX_RIP(context) == (uintptr_t)code + length;
}

int main(void) {
    ucontext_t context;
    memset(&context, 0, sizeof(context));
    Device device = {.value = 0x89ABCDEF};
    // MOV EAX,[RDI]: a 32-bit load zeroes the high half of native RAX.
    const uint8_t load32[] = {0x8B, 0x07};
    context.uc_mcontext.gregs[REG_RAX] = -1;
    CHECK(emulate(&context, &device, load32, sizeof(load32)));
    CHECK(context.uc_mcontext.gregs[REG_RAX] == 0x89ABCDEF && device.reads == 1 && device.width == 4);
    // MOV [RDI],ECX: context register numbering differs from x86 ModRM.
    const uint8_t store32[] = {0x89, 0x0F};
    context.uc_mcontext.gregs[REG_RCX] = 0x76543210;
    CHECK(emulate(&context, &device, store32, sizeof(store32)));
    CHECK(device.stored == 0x76543210 && device.writes == 1 && device.width == 4);
    // MOVZX ECX,byte [RDX]: width and zero extension survive signal return.
    const uint8_t byte_load[] = {0x0F, 0xB6, 0x0A};
    CHECK(emulate(&context, &device, byte_load, sizeof(byte_load)));
    CHECK(context.uc_mcontext.gregs[REG_RCX] == 0xEF && device.width == 1);
    // CMP dword [RDI],imm8: polling branches consume native saved flags.
    const uint8_t compare[] = {0x83, 0x3F, 0x12};
    device.value = 0x12;
    CHECK(emulate(&context, &device, compare, sizeof(compare)));
    CHECK((MMIO_CTX_EFL(&context) & 0x40) != 0); // EFLAGS bit 6: ZF.
    device.value = 0x11;
    CHECK(emulate(&context, &device, compare, sizeof(compare)));
    CHECK((MMIO_CTX_EFL(&context) & 0x41) == 1); // Borrow sets CF, clears ZF.
    // MOV AH,[RAX]: preserve every bit except bits 15:8 on a byte load.
    const uint8_t high_byte[] = {0x8A, 0x20};
    context.uc_mcontext.gregs[REG_RAX] = 0x12345678;
    device.value = 0xAB;
    CHECK(emulate(&context, &device, high_byte, sizeof(high_byte)));
    CHECK(context.uc_mcontext.gregs[REG_RAX] == 0x1234AB78);
    // MOV dword [RDI],0x12345678: full immediate length must be consumed.
    const uint8_t immediate[] = {0xC7, 0x07, 0x78, 0x56, 0x34, 0x12};
    CHECK(emulate(&context, &device, immediate, sizeof(immediate)));
    CHECK(device.stored == 0x12345678 && device.width == 4);
    // UD2 is unsupported. It must perform no access and leave PC unchanged.
    const uint8_t unknown[] = {0x0F, 0x0B};
    unsigned accesses = device.reads + device.writes;
    MMIO_CTX_RIP(&context) = (uintptr_t)unknown;
    CHECK(!mmio_emulate_host(&context, 0x10, &device, read_register, write_register));
    CHECK(MMIO_CTX_RIP(&context) == (uintptr_t)unknown && accesses == device.reads + device.writes);
    puts("8 Linux MMIO instruction/context checks passed");
    return 0;
}
