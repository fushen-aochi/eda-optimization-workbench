module top(input clk, input a, input b, input c, input d, input s, output reg y, output z);
  wire [3:0] sum = {3'b000, a} + {3'b000, b} + {3'b000, c};
  wire slow = (sum[2] & d) ^ (a & b);
  wire fast = a | c;
  wire muxed = s ? slow : fast;

  always @(posedge clk)
    y <= muxed;

  assign z = muxed & d;
endmodule
