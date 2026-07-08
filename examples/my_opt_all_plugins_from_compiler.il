autoidx 1
module \top
  wire input 1 \clk
  wire input 2 \s
  wire input 3 \a
  wire input 4 \b
  wire input 5 \c
  wire input 6 \d
  wire input 7 \e
  wire input 8 \f
  wire input 9 width 4 \x
  wire input 10 width 4 \u
  wire input 11 width 4 \v
  wire input 12 width 4 \w
  wire output 13 \y_expr
  wire output 14 \y_reduce
  wire output 15 \y_mux
  wire output 16 width 8 \y_strength
  wire output 17 \y_cse
  wire output 18 \y_cover
  wire output 19 \y_qm
  wire output 20 \y_rewrite
  wire output 21 width 4 \y_share
  wire output 22 \y_timing
  wire \expr_keep
  wire \expr_zero
  wire \expr_one
  wire \reduce_const_and
  wire \reduce_const_or
  wire \reduce_const_xor
  wire \cse_ab_1
  wire \cse_ab_2
  wire \cse_cd_1
  wire \cse_cd_2
  wire \dead_0
  wire \dead_1
  wire \dead_2
  wire \dead_3
  wire \cov0
  wire \cov1
  wire \cov2
  wire \qm0
  wire \qm1
  wire \qm2
  wire \qm3
  wire \rw0
  wire \rw1
  wire \rw2
  wire \rw3
  wire \rw4
  wire width 4 \add_a
  wire width 4 \add_b
  wire width 8 \timing_mul
  wire width 8 \timing_add
  wire width 8 \timing_logic
  wire \__t1
  wire \__t10
  wire \__t11
  wire \__t12
  wire \__t13
  wire \__t14
  wire \__t15
  wire width 8 \__t16
  wire \__t17
  wire \__t18
  wire \__t19
  wire \__t2
  wire \__t20
  wire \__t21
  wire \__t22
  wire \__t23
  wire \__t24
  wire \__t25
  wire \__t26
  wire \__t27
  wire \__t28
  wire \__t29
  wire \__t3
  wire \__t30
  wire \__t31
  wire \__t32
  wire \__t33
  wire \__t34
  wire \__t35
  wire \__t36
  wire \__t37
  wire \__t38
  wire \__t39
  wire \__t4
  wire \__t40
  wire \__t41
  wire \__t42
  wire \__t43
  wire \__t44
  wire \__t45
  wire \__t46
  wire \__t47
  wire \__t48
  wire \__t49
  wire \__t5
  wire \__t50
  wire \__t51
  wire \__t52
  wire \__t53
  wire \__t54
  wire \__t55
  wire \__t56
  wire \__t57
  wire \__t58
  wire \__t59
  wire \__t6
  wire \__t60
  wire \__t61
  wire \__t62
  wire \__t63
  wire \__t64
  wire \__t65
  wire \__t66
  wire \__t67
  wire \__t68
  wire width 4 \__t69
  wire \__t7
  wire width 4 \__t70
  wire width 4 \__t71
  wire width 8 \__t72
  wire width 8 \__t73
  wire width 8 \__t74
  wire \__t75
  wire \__t76
  wire \__t77
  wire \__t78
  wire width 8 \__t79
  wire \__t8
  wire \__t80
  wire \__t9
  cell $and \$and_1
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B 1'1
    connect \Y \__t1
  end
  cell $xor \$xor_2
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \b
    connect \B 1'0
    connect \Y \__t2
  end
  cell $or \$or_3
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \c
    connect \B 1'0
    connect \Y \__t3
  end
  cell $and \$and_4
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \expr_keep
    connect \B \expr_zero
    connect \Y \__t4
  end
  cell $and \$and_5
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t4
    connect \B \expr_one
    connect \Y \__t5
  end
  cell $reduce_and \$reduce_and_6
    parameter \A_SIGNED 0
    parameter \A_WIDTH 4
    parameter \Y_WIDTH 1
    connect \A 4'1111
    connect \Y \__t6
  end
  cell $reduce_or \$reduce_or_7
    parameter \A_SIGNED 0
    parameter \A_WIDTH 4
    parameter \Y_WIDTH 1
    connect \A 4'0000
    connect \Y \__t7
  end
  cell $reduce_xor \$reduce_xor_8
    parameter \A_SIGNED 0
    parameter \A_WIDTH 4
    parameter \Y_WIDTH 1
    connect \A 4'0101
    connect \Y \__t8
  end
  cell $not \$not_9
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \reduce_const_or
    connect \Y \__t9
  end
  cell $and \$and_10
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \reduce_const_and
    connect \B \__t9
    connect \Y \__t10
  end
  cell $not \$not_11
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \reduce_const_xor
    connect \Y \__t11
  end
  cell $and \$and_12
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t10
    connect \B \__t11
    connect \Y \__t12
  end
  cell $or \$or_13
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B \b
    connect \Y \__t13
  end
  cell $and \$and_14
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \c
    connect \B \d
    connect \Y \__t14
  end
  cell $mux \$mux_15
    parameter \WIDTH 1
    connect \A \__t14
    connect \B \__t13
    connect \S 1'1
    connect \Y \__t15
  end
  cell $mul \$mul_16
    parameter \A_SIGNED 0
    parameter \A_WIDTH 4
    parameter \B_SIGNED 0
    parameter \B_WIDTH 8
    parameter \Y_WIDTH 8
    connect \A \x
    connect \B 8'10100000
    connect \Y \__t16
  end
  cell $and \$and_17
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B \b
    connect \Y \__t17
  end
  cell $and \$and_18
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \b
    connect \B \a
    connect \Y \__t18
  end
  cell $xor \$xor_19
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \c
    connect \B \d
    connect \Y \__t19
  end
  cell $xor \$xor_20
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \d
    connect \B \c
    connect \Y \__t20
  end
  cell $or \$or_21
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \cse_ab_1
    connect \B \cse_cd_1
    connect \Y \__t21
  end
  cell $or \$or_22
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \cse_ab_2
    connect \B \cse_cd_2
    connect \Y \__t22
  end
  cell $and \$and_23
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t21
    connect \B \__t22
    connect \Y \__t23
  end
  cell $and \$and_24
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B \f
    connect \Y \__t24
  end
  cell $or \$or_25
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \dead_0
    connect \B \b
    connect \Y \__t25
  end
  cell $xor \$xor_26
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \dead_1
    connect \B \c
    connect \Y \__t26
  end
  cell $and \$and_27
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \dead_2
    connect \B \d
    connect \Y \__t27
  end
  cell $and \$and_28
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B \b
    connect \Y \__t28
  end
  cell $and \$and_29
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B \b
    connect \Y \__t29
  end
  cell $and \$and_30
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t29
    connect \B \c
    connect \Y \__t30
  end
  cell $and \$and_31
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B \b
    connect \Y \__t31
  end
  cell $and \$and_32
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t31
    connect \B \c
    connect \Y \__t32
  end
  cell $and \$and_33
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t32
    connect \B \d
    connect \Y \__t33
  end
  cell $or \$or_34
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \cov0
    connect \B \cov1
    connect \Y \__t34
  end
  cell $or \$or_35
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t34
    connect \B \cov2
    connect \Y \__t35
  end
  cell $and \$and_36
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B \b
    connect \Y \__t36
  end
  cell $and \$and_37
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t36
    connect \B \c
    connect \Y \__t37
  end
  cell $and \$and_38
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B \b
    connect \Y \__t38
  end
  cell $not \$not_39
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \c
    connect \Y \__t39
  end
  cell $and \$and_40
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t38
    connect \B \__t39
    connect \Y \__t40
  end
  cell $not \$not_41
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \b
    connect \Y \__t41
  end
  cell $and \$and_42
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B \__t41
    connect \Y \__t42
  end
  cell $and \$and_43
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t42
    connect \B \c
    connect \Y \__t43
  end
  cell $not \$not_44
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \b
    connect \Y \__t44
  end
  cell $and \$and_45
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B \__t44
    connect \Y \__t45
  end
  cell $not \$not_46
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \c
    connect \Y \__t46
  end
  cell $and \$and_47
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t45
    connect \B \__t46
    connect \Y \__t47
  end
  cell $or \$or_48
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \qm0
    connect \B \qm1
    connect \Y \__t48
  end
  cell $or \$or_49
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t48
    connect \B \qm2
    connect \Y \__t49
  end
  cell $or \$or_50
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t49
    connect \B \qm3
    connect \Y \__t50
  end
  cell $and \$and_51
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B \b
    connect \Y \__t51
  end
  cell $and \$and_52
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t51
    connect \B \c
    connect \Y \__t52
  end
  cell $and \$and_53
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B \b
    connect \Y \__t53
  end
  cell $and \$and_54
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t53
    connect \B \d
    connect \Y \__t54
  end
  cell $and \$and_55
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B \b
    connect \Y \__t55
  end
  cell $and \$and_56
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t55
    connect \B \e
    connect \Y \__t56
  end
  cell $and \$and_57
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B \b
    connect \Y \__t57
  end
  cell $not \$not_58
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \c
    connect \Y \__t58
  end
  cell $and \$and_59
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t57
    connect \B \__t58
    connect \Y \__t59
  end
  cell $not \$not_60
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \d
    connect \Y \__t60
  end
  cell $and \$and_61
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t59
    connect \B \__t60
    connect \Y \__t61
  end
  cell $and \$and_62
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B \b
    connect \Y \__t62
  end
  cell $and \$and_63
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t62
    connect \B \c
    connect \Y \__t63
  end
  cell $and \$and_64
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t63
    connect \B \d
    connect \Y \__t64
  end
  cell $or \$or_65
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \rw0
    connect \B \rw1
    connect \Y \__t65
  end
  cell $or \$or_66
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t65
    connect \B \rw2
    connect \Y \__t66
  end
  cell $or \$or_67
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t66
    connect \B \rw3
    connect \Y \__t67
  end
  cell $or \$or_68
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t67
    connect \B \rw4
    connect \Y \__t68
  end
  cell $add \$add_69
    parameter \A_SIGNED 0
    parameter \A_WIDTH 4
    parameter \B_SIGNED 0
    parameter \B_WIDTH 4
    parameter \Y_WIDTH 4
    connect \A \u
    connect \B \v
    connect \Y \__t69
  end
  cell $add \$add_70
    parameter \A_SIGNED 0
    parameter \A_WIDTH 4
    parameter \B_SIGNED 0
    parameter \B_WIDTH 4
    parameter \Y_WIDTH 4
    connect \A \u
    connect \B \w
    connect \Y \__t70
  end
  cell $mux \$mux_71
    parameter \WIDTH 4
    connect \A \add_b
    connect \B \add_a
    connect \S \s
    connect \Y \__t71
  end
  cell $mul \$mul_72
    parameter \A_SIGNED 0
    parameter \A_WIDTH 4
    parameter \B_SIGNED 0
    parameter \B_WIDTH 8
    parameter \Y_WIDTH 8
    connect \A \x
    connect \B 8'11000000
    connect \Y \__t72
  end
  cell $add \$add_73
    parameter \A_SIGNED 0
    parameter \A_WIDTH 8
    parameter \B_SIGNED 0
    parameter \B_WIDTH 4
    parameter \Y_WIDTH 8
    connect \A \timing_mul
    connect \B \u
    connect \Y \__t73
  end
  cell $and \$and_74
    parameter \A_SIGNED 0
    parameter \A_WIDTH 8
    parameter \B_SIGNED 0
    parameter \B_WIDTH 8
    parameter \Y_WIDTH 8
    connect \A \timing_add
    connect \B 8'00010000
    connect \Y \__t74
  end
  cell $or \$or_75
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B \b
    connect \Y \__t75
  end
  cell $or \$or_76
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t75
    connect \B \c
    connect \Y \__t76
  end
  cell $or \$or_77
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t76
    connect \B \d
    connect \Y \__t77
  end
  cell $or \$or_78
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t77
    connect \B \e
    connect \Y \__t78
  end
  cell $and \$and_79
    parameter \A_SIGNED 0
    parameter \A_WIDTH 8
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 8
    connect \A \__t74
    connect \B \__t78
    connect \Y \__t79
  end
  cell $reduce_or \$reduce_or_80
    parameter \A_SIGNED 0
    parameter \A_WIDTH 8
    parameter \Y_WIDTH 1
    connect \A \timing_logic
    connect \Y \__t80
  end
  connect \expr_keep \__t1
  connect \expr_zero \__t2
  connect \expr_one \__t3
  connect \y_expr \__t5
  connect \reduce_const_and \__t6
  connect \reduce_const_or \__t7
  connect \reduce_const_xor \__t8
  connect \y_reduce \__t12
  connect \y_mux \__t15
  connect \y_strength \__t16
  connect \cse_ab_1 \__t17
  connect \cse_ab_2 \__t18
  connect \cse_cd_1 \__t19
  connect \cse_cd_2 \__t20
  connect \y_cse \__t23
  connect \dead_0 \__t24
  connect \dead_1 \__t25
  connect \dead_2 \__t26
  connect \dead_3 \__t27
  connect \cov0 \__t28
  connect \cov1 \__t30
  connect \cov2 \__t33
  connect \y_cover \__t35
  connect \qm0 \__t37
  connect \qm1 \__t40
  connect \qm2 \__t43
  connect \qm3 \__t47
  connect \y_qm \__t50
  connect \rw0 \__t52
  connect \rw1 \__t54
  connect \rw2 \__t56
  connect \rw3 \__t61
  connect \rw4 \__t64
  connect \y_rewrite \__t68
  connect \add_a \__t69
  connect \add_b \__t70
  connect \y_share \__t71
  connect \timing_mul \__t72
  connect \timing_add \__t73
  connect \timing_logic \__t79
  connect \y_timing \__t80
end
