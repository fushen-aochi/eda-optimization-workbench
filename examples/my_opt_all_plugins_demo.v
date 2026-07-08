module top(clk,s,a,b,c,d,e,f,x,u,v,w,y_expr,y_reduce,y_mux,y_strength,y_cse,y_cover,y_qm,y_rewrite,y_share,y_timing);
input clk,s,a,b,c,d,e,f;
input [3:0] x,u,v,w;
output y_expr,y_reduce,y_mux,y_cse,y_cover,y_qm,y_rewrite,y_timing;
output [7:0] y_strength;
output [3:0] y_share;
wire expr_keep,expr_zero,expr_one;
wire reduce_const_and,reduce_const_or,reduce_const_xor;
wire cse_ab_1,cse_ab_2,cse_cd_1,cse_cd_2;
wire dead_0,dead_1,dead_2,dead_3;
wire cov0,cov1,cov2;
wire qm0,qm1,qm2,qm3;
wire rw0,rw1,rw2,rw3,rw4;
wire [3:0] add_a,add_b;
wire [7:0] timing_mul,timing_add;
wire [7:0] timing_logic;

assign expr_keep = a & 1'b1;
assign expr_zero = b ^ 1'b0;
assign expr_one = c | 1'b0;
assign y_expr = expr_keep & expr_zero & expr_one;

assign reduce_const_and = &4'b1111;
assign reduce_const_or = |4'b0000;
assign reduce_const_xor = ^4'b1010;
assign y_reduce = reduce_const_and & ~reduce_const_or & ~reduce_const_xor;

assign y_mux = 1'b1 ? (a | b) : (c & d);

assign y_strength = x * 8'd5;

assign cse_ab_1 = a & b;
assign cse_ab_2 = b & a;
assign cse_cd_1 = c ^ d;
assign cse_cd_2 = d ^ c;
assign y_cse = (cse_ab_1 | cse_cd_1) & (cse_ab_2 | cse_cd_2);

assign dead_0 = a & f;
assign dead_1 = dead_0 | b;
assign dead_2 = dead_1 ^ c;
assign dead_3 = dead_2 & d;

assign cov0 = a & b;
assign cov1 = a & b & c;
assign cov2 = a & b & c & d;
assign y_cover = cov0 | cov1 | cov2;

assign qm0 = a & b & c;
assign qm1 = a & b & ~c;
assign qm2 = a & ~b & c;
assign qm3 = a & ~b & ~c;
assign y_qm = qm0 | qm1 | qm2 | qm3;

assign rw0 = a & b & c;
assign rw1 = a & b & d;
assign rw2 = a & b & e;
assign rw3 = a & b & ~c & ~d;
assign rw4 = a & b & c & d;
assign y_rewrite = rw0 | rw1 | rw2 | rw3 | rw4;

assign add_a = u + v;
assign add_b = u + w;
assign y_share = s ? add_a : add_b;

assign timing_mul = x * 8'd3;
assign timing_add = timing_mul + u;
assign timing_logic = (timing_add & 8'd8) & (a | b | c | d | e);
assign y_timing = |timing_logic;
endmodule
