module hls_board(input wire clk,output wire led);
    reg [7:0] counter=0;
    reg [3:0] startup=0;
    wire reset=!startup[3];
    wire ready,valid;
    wire [7:0] result;
    always @(posedge clk) begin
        if(!startup[3]) startup<=startup+1'b1;
        if(ready)counter<=counter+1'b1;
    end
    crc_kernel kernel(.clk(clk),.reset(reset),.in_valid(1'b1),.in_ready(ready),
        .out_valid(valid),.out_ready(1'b1),.arg_crc(8'h0),.arg_data(counter),.result(result));
    assign led=valid&&result[0];
endmodule
