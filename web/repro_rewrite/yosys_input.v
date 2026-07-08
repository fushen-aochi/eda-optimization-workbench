module logic_demo(a,b,c,d,y);
input a,b,c,d;
output y;
wire n1,n2,n3;
assign n1 = a & b;
assign n2 = a & c;
assign n3 = n1 | n2;
assign y = n3 | (d & 0);
endmodule
