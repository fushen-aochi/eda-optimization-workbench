module qm_xor_cover(input a, input b, input c, output y);
  wire p0 = (~a) & (~b) & c;
  wire p1 = (~a) & b & (~c);
  wire p2 = a & (~b) & (~c);
  wire p3 = a & b & c;
  assign y = (p0 | p1) | (p2 | p3);
endmodule
