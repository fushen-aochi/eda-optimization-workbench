module cover_expand_irredundant(input a, input b, input c, output y);
  wire p0 = (~a) & b & c;
  wire p1 = a & b & c;
  wire p2 = a & b & (~c);
  wire p3 = a & b;
  assign y = (p0 | p1) | (p2 | p3);
endmodule
