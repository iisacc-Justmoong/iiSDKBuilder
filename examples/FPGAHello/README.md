# FPGAHello

`blinker.v` implements a clocked LED counter. The iCE40 flow compiles it through Yosys, nextpnr and IceStorm into an HX1K TQ144 configuration bitstream using the explicit iCEstick pin constraints. `tb.sv` executes the same logic through a Verilator C++ AOT simulation and checks four transitions. The native simulation and physical FPGA programming are separate report states.
