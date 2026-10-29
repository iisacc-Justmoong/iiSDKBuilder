#include <stdint.h>
static int32_t helper(int32_t a, int32_t b) { return a * 3 + b; }
extern "C" int32_t signed_mix(int8_t a, uint16_t b) {
    int32_t value = int32_t(a) + b;
    if (value < 0)
        return value >> 2;
    value += helper(a, b) / 3;
    return value;
}
extern "C" uint64_t widen(int8_t a, uint8_t b) { return uint64_t(a) ^ (uint64_t(b) << 40); }
extern "C" uint32_t early(uint32_t a) {
    if (a < 5)
        return a + 3;
    if (a > 100)
        return a - 7;
    return a ^ 0x5a;
}
extern "C" uint16_t short_circuit(uint8_t a, uint8_t b) {
    unsigned x = a;
    bool chosen = (a != 0) && (++x > b);
    x += ++x; // C++23 sequences the RHS before reading the compound-assignment LHS.
    return uint16_t(x + (chosen ? 256 : 0));
}
extern "C" uint32_t array_sum(const uint16_t (&items)[4]) {
    uint32_t total = 0;
    for (unsigned i = 0; i < 4; ++i)
        total += items[i];
    return total;
}
extern "C" uint32_t local_array(uint16_t a) {
    const uint16_t coefficients[4] = {1, 2};
    uint32_t total = 0;
    for (unsigned i = 0; i < 4; ++i)
        total += coefficients[i] * a;
    return total;
}

extern "C" unsigned compound_constant() {
    unsigned x = 3;
    x += ++x;
    return x;
}
