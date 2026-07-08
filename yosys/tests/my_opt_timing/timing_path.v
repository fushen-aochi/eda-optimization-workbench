module timing_path(input a, input b, input c, input d, input s, output y);
  wire [1:0] sum = {1'b0, a} + {1'b0, b};
  wire carry = sum[1];
  wire p0 = carry & c;
  wire p1 = a ^ d;
  assign y = s ? p0 : p1;
endmodule
