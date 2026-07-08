module top(input a, input b, input c, input d, output y, output z);
  wire ab;
  wire ac;
  wire abc;
  wire abd;
  wire redundant;

  assign ab = a & b;
  assign ac = a & c;
  assign abc = a & b & c;
  assign abd = a & b & d;

  assign redundant = ab | abc;
  assign y = ab | ac;
  assign z = redundant | abd;
endmodule
