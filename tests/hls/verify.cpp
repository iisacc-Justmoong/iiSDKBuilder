#include "../../examples/HLSHello/crc8.cpp"
#include "Vhls_verify.h"
#include "kernels.cpp"
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <verilated.h>
struct Results final {
    uint64_t wide;
    uint32_t mixed, early, array, local;
    uint16_t short_value;
    uint8_t crc;
    bool operator==(const Results &) const = default;
};
static Results reference(uint64_t data) {
    const auto a = uint8_t(data), b = uint8_t(data >> 8);
    uint16_t items[4];
    for (unsigned i = 0; i < 4; ++i)
        items[i] = uint16_t(data >> (i * 16));
    return {widen(int8_t(a), b),
            uint32_t(signed_mix(int8_t(a), uint16_t(data >> 16))),
            early(uint32_t(data)),
            array_sum(items),
            local_array(uint16_t(data)),
            short_circuit(a, b),
            crc8(a, b)};
}
static Results actual(const Vhls_verify &d) {
    return {d.wide, d.mixed, d.early_result, d.array_result, d.local_result, d.short_result, d.crc};
}
int main(int argc, char **argv) {
    try {
        auto context = std::make_unique<VerilatedContext>();
        context->commandArgs(argc, argv);
        auto device = std::make_unique<Vhls_verify>(context.get());
        auto clock = [&] {
            device->clk = 1;
            device->eval();
            context->timeInc(1);
            device->clk = 0;
            device->eval();
            context->timeInc(1);
        };
        device->clk = 0;
        device->reset = 1;
        device->in_valid = 0;
        device->out_ready = 0;
        device->payload = 0;
        device->eval();
        clock();
        clock();
        device->reset = 0;
        std::deque<Results> pending;
        uint64_t random = 0x5329aed41f7ULL;
        unsigned issued = 0, received = 0, stalled = 0;
        constexpr unsigned exhaustive = 65536, total = exhaustive + 10000;
        Results held{};
        bool holding = false;
        for (unsigned cycle = 0; cycle < total * 4 + 100; ++cycle) {
            device->out_ready = (cycle % 7) < 5;
            device->in_valid = issued < total && (cycle % 11) != 0;
            uint64_t payload = issued < exhaustive ? issued : random;
            device->payload = payload;
            device->eval();
            if (device->protocol_error)
                throw std::runtime_error("kernel handshake mismatch");
            if (holding && (!device->out_valid || actual(*device) != held))
                throw std::runtime_error("output changed under backpressure");
            holding = device->out_valid && !device->out_ready;
            if (holding) {
                held = actual(*device);
                ++stalled;
            }
            if (device->out_valid && device->out_ready) {
                if (pending.empty() || actual(*device) != pending.front())
                    throw std::runtime_error("C++/RTL mismatch at transaction " + std::to_string(received));
                pending.pop_front();
                ++received;
            }
            if (device->in_valid && device->in_ready) {
                pending.push_back(reference(payload));
                ++issued;
                random ^= random << 13;
                random ^= random >> 7;
                random ^= random << 17;
            }
            clock();
            if (received == total)
                break;
        }
        if (received != total || !pending.empty() || !stalled)
            throw std::runtime_error("transactions/stalls not covered");
        // Reset must discard an occupied result and close input admission.
        device->in_valid = 1;
        device->out_ready = 0;
        device->eval();
        clock();
        device->reset = 1;
        device->eval();
        if (device->in_ready)
            throw std::runtime_error("input admitted during reset");
        clock();
        if (device->out_valid)
            throw std::runtime_error("reset did not clear pending output");
        std::cout << "HLS_CPP_RTL_EQUIVALENCE_OK transactions=" << received
                  << " kernels=7 crc_pairs=65536 stalls=" << stalled << '\n';
        device->final();
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
