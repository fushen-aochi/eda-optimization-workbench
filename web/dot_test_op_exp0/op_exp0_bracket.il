autoidx 1
module \op_exp0_bracket
  wire input 1 \a
  wire input 2 \b
  wire input 3 \c
  wire input 4 \d
  wire input 5 \e
  wire output 6 \q
  wire \sum1
  wire \sum2
  wire \sum3
  wire \__t1
  wire \__t2
  wire \__t3
  wire \__t4
  wire \__t5
  wire \__t6
  wire \__t7
  wire \__t8
  cell $add \$add_1
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \b
    connect \B \c
    connect \Y \__t1
  end
  cell $add \$add_2
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B \__t1
    connect \Y \__t2
  end
  cell $add \$add_3
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \b
    connect \B \d
    connect \Y \__t3
  end
  cell $add \$add_4
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B \__t3
    connect \Y \__t4
  end
  cell $add \$add_5
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \b
    connect \B \e
    connect \Y \__t5
  end
  cell $add \$add_6
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B \__t5
    connect \Y \__t6
  end
  cell $and \$and_7
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \sum1
    connect \B \sum2
    connect \Y \__t7
  end
  cell $and \$and_8
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \__t7
    connect \B \sum3
    connect \Y \__t8
  end
  connect \sum1 \__t2
  connect \sum2 \__t4
  connect \sum3 \__t6
  connect \q \__t8
end
