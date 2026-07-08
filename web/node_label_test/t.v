module node_label_demo(a,b,c,out_d);
input a,b,c;
output out_d;
wire A,B,C,D;
assign A = a & 1'b1;
assign B = b & 1'b1;
assign C = c & 1'b1;
assign D = A & B;
assign out_d = D;
endmodule