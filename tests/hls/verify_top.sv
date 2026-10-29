module hls_verify(input wire clk,input wire reset,input wire in_valid,input wire out_ready,
    input wire [63:0] payload,output wire in_ready,output wire out_valid,output wire protocol_error,
    output wire [7:0] crc,output wire signed [31:0] mixed,output wire [63:0] wide,
    output wire [31:0] early_result,output wire [15:0] short_result,
    output wire [31:0] array_result,output wire [31:0] local_result);
    wire [6:0] ready,valid;
    assign in_ready=&ready;
    assign out_valid=&valid;
    assign protocol_error=(ready!={7{ready[0]}})||(valid!={7{valid[0]}});
    hls_crc8 a(.clk(clk),.reset(reset),.in_valid(in_valid),.in_ready(ready[0]),.out_valid(valid[0]),.out_ready(out_ready),.arg_crc(payload[7:0]),.arg_data(payload[15:8]),.result(crc));
    hls_signed_mix b(.clk(clk),.reset(reset),.in_valid(in_valid),.in_ready(ready[1]),.out_valid(valid[1]),.out_ready(out_ready),.arg_a(payload[7:0]),.arg_b(payload[31:16]),.result(mixed));
    hls_widen c(.clk(clk),.reset(reset),.in_valid(in_valid),.in_ready(ready[2]),.out_valid(valid[2]),.out_ready(out_ready),.arg_a(payload[7:0]),.arg_b(payload[15:8]),.result(wide));
    hls_early d(.clk(clk),.reset(reset),.in_valid(in_valid),.in_ready(ready[3]),.out_valid(valid[3]),.out_ready(out_ready),.arg_a(payload[31:0]),.result(early_result));
    hls_short_circuit e(.clk(clk),.reset(reset),.in_valid(in_valid),.in_ready(ready[4]),.out_valid(valid[4]),.out_ready(out_ready),.arg_a(payload[7:0]),.arg_b(payload[15:8]),.result(short_result));
    hls_array_sum f(.clk(clk),.reset(reset),.in_valid(in_valid),.in_ready(ready[5]),.out_valid(valid[5]),.out_ready(out_ready),.arg_items(payload),.result(array_result));
    hls_local_array g(.clk(clk),.reset(reset),.in_valid(in_valid),.in_ready(ready[6]),.out_valid(valid[6]),.out_ready(out_ready),.arg_a(payload[15:0]),.result(local_result));
endmodule
