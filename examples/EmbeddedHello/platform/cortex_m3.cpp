#include <stdint.h>
extern "C" {
extern uint32_t _stack_top, _data_start, _data_end, _data_load, _bss_start, _bss_end;
int main();
void board_write(const char *text) noexcept {
    register uint32_t op asm("r0") = 4;
    register const char *bytes asm("r1") = text;
    asm volatile("bkpt 0xab" : "+r"(op) : "r"(bytes) : "memory");
}
void Reset_Handler() {
    auto *src = &_data_load;
    for (auto *p = &_data_start; p < &_data_end;)
        *p++ = *src++;
    for (auto *p = &_bss_start; p < &_bss_end;)
        *p++ = 0;
    const uint32_t result[] = {0x20026, uint32_t(main())};
    register uint32_t op asm("r0") = 0x20;
    register const uint32_t *value asm("r1") = result;
    asm volatile("bkpt 0xab" : "+r"(op) : "r"(value) : "memory");
    for (;;)
        asm volatile("wfi");
}
void Default_Handler() {
    for (;;)
        asm volatile("wfi");
}
__attribute__((section(".isr_vector"), used)) void (*const vectors[])() = {
    reinterpret_cast<void (*)()>(&_stack_top), Reset_Handler, Default_Handler, Default_Handler};
void *memset(void *p, int c, unsigned n) {
    auto *b = static_cast<volatile unsigned char *>(p);
    for (unsigned i = 0; i < n; ++i)
        b[i] = static_cast<unsigned char>(c);
    return p;
}
void *memcpy(void *d, const void *s, unsigned n) {
    auto *a = static_cast<volatile unsigned char *>(d);
    auto *b = static_cast<const volatile unsigned char *>(s);
    for (unsigned i = 0; i < n; ++i)
        a[i] = b[i];
    return d;
}
void __aeabi_memclr4(void *p, unsigned n) { memset(p, 0, n); }
void __aeabi_memclr(void *p, unsigned n) { memset(p, 0, n); }
}
