module ml_kernel_example(input a, input b, input c, input d, output y);
  wire p0 = a & b & c;
  wire p1 = b & c & d;
  wire p2 = a & d;
  assign y = (p0 | p1) | p2;
endmodule
