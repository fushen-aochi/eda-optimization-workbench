module top(input a, input b, input c, input d, output absorb, output expand, output exact, output internal_out);
  wire t0, t1, t2, t3;
  wire r0, r1, r2;
  wire k0, k1, k2;
  wire internal_root;

  assign absorb = (a & b) | (a & b & c) | (a & b & c & d);

  assign t0 = a & b & c;
  assign t1 = a & b & ~c;
  assign t2 = a & ~b & c;
  assign t3 = a & ~b & ~c;
  assign expand = t0 | t1 | t2 | t3;

  assign r0 = a & b & c;
  assign r1 = a & b & ~c;
  assign r2 = a & ~b & c;
  assign exact = r0 | r1 | r2;

  assign k0 = a & b & c;
  assign k1 = a & b & d;
  assign k2 = a & b & ~c & ~d;
  assign internal_root = k0 | k1 | k2;
  assign internal_out = internal_root | (a & b & c & d);
endmodule
