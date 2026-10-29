#include <stdint.h>
// CRC-8 polynomial 0x07; one byte per ready/valid transaction.
extern "C" uint8_t crc8(uint8_t crc, uint8_t data) {
    uint8_t value = crc ^ data;
    for (unsigned i = 0; i < 8; ++i) {
        if (value & 0x80)
            value = (value << 1) ^ 0x07;
        else
            value = value << 1;
    }
    return value;
}
