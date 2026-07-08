module qm_majority(input a, input b, input c, output y);
  wire ab = a & b;
  wire ac = a & c;
  wire bc = b & c;
  assign y = (ab | ac) | bc;
endmodule
