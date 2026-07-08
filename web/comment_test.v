module comment_demo(a,b,y);
input a,b;
output y;
/* block comment */
wire n;
// line comment
assign n = a & 1'b1;
assign y = n | b;
endmodule
