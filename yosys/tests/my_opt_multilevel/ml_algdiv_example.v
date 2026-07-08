module ml_algdiv_example(input a, input c, input d, input e, output y);
  wire p0 = a & c & d;
  wire p1 = c & e;
  wire p2 = a & c;
  wire p3 = a & e;
  assign y = (p0 | p1) | (p2 | p3);
endmodule
