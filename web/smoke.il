autoidx 1
module \demo
  wire input 1 \a
  wire input 2 \b
  wire output 3 \y
  wire \t
  wire \__t1
  wire \__t2
  cell $and \$and_1
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \B_SIGNED 0
    parameter \B_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \a
    connect \B \b
    connect \Y \__t1
  end
  cell $not \$not_2
    parameter \A_SIGNED 0
    parameter \A_WIDTH 1
    parameter \Y_WIDTH 1
    connect \A \t
    connect \Y \__t2
  end
  connect \t \__t1
  connect \y \__t2
end
