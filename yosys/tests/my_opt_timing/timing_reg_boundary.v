module timing_reg_boundary(input clk, input a, input b, input c, output reg y);
  wire [1:0] sum = {1'b0, a} + {1'b0, b};
  wire logic_path = sum[1] & c;
  always @(posedge clk)
    y <= logic_path;
endmodule
