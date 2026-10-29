module blinker(input wire clk,output wire led);
    reg [3:0] count=0;
    always @(posedge clk) count <= count+1'b1;
    assign led=count[3];
endmodule
