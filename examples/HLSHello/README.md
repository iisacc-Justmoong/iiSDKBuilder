# HLSHello

`crc8.cpp` is the actual C++23 synthesis input. The builder lowers the bounded CRC-8 algorithm to a typed dataflow and SystemVerilog. `board.v` connects its generated `crc_kernel` ready/valid interface to a counter and LED. `ice40.pcf` is specifically for the iCEstick HX1K TQ144 clock/LED pins.

Run a portable RTL build with the example manifest:

```sh
iisdk-builder build --manifest examples/HLSHello/iiSDKBuilder.json --sdk-root /path/to/SDK
```

The manifest uses `clang++` and `yosys` from the kit environment. To generate an iCE40 image, choose `implementation: ice40`, `part: hx1k`, `family: tq144`, `top: hls_board`, the wrapper and constraints, and a clock period of 83333 ps. The same emitted SystemVerilog can be implemented through the Vivado or Quartus adapters with their actual part/constraint/tool settings.

See [HLS](../../docs/HLS.md) for the exact supported C++ subset. The FPGA contains gates and registers; Core manages build-time process ownership on the host. Device programming is a separate explicit step.
