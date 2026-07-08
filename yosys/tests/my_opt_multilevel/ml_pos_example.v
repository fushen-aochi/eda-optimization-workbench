module ml_pos_example(input x, input a, input b, input c, output y);
  wire s0 = x | a;
  wire s1 = x | b;
  wire s2 = x | c;
  assign y = (s0 & s1) & s2;
endmodule
