module demo(a, b, y);
input a, b;
output y;
wire t;
assign t = a & b;
assign y = ~t;
endmodule