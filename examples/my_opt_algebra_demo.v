module top(input a, input b, input c, input d, input e, output y, output z, output w);
  wire p1;
  wire p2;
  wire p3;
  wire q1;
  wire q2;
  wire q3;
  wire r1;
  wire r2;

  assign p1 = a & b & c;
  assign p2 = a & b & d;
  assign p3 = a & b & e;
  assign y = p1 | p2 | p3;

  assign q1 = a | b | c;
  assign q2 = a | b | d;
  assign q3 = a | b | e;
  assign z = q1 & q2 & q3;

  assign r1 = a & c;
  assign r2 = a & d;
  assign w = r1 | r2 | b;
endmodule
