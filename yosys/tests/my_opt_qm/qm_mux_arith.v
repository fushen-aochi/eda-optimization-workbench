module qm_mux_arith(input a, input b, input s, output y);
  wire [1:0] sum = {1'b0, a} + {1'b0, b};
  wire carry = sum[1];
  assign y = s ? carry : (a ^ b);
endmodule
