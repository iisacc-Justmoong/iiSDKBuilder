module tb;
    reg clk=0;wire led;reg previous=0;integer changes=0;
    blinker dut(.clk(clk),.led(led));
    always #5 clk=~clk;
    initial begin
        repeat(40) begin @(negedge clk);if(led!=previous)changes=changes+1;previous=led;end
        if(changes<4)$fatal(1,"LED did not execute the expected native clocked logic");
        $display("II_FPGA_AOT_OK");$finish;
    end
endmodule
