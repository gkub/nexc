module {
  func.func @less_than(%arg0: i32, %arg1: i32) -> i1 {
    %0 = arith.cmpi slt, %arg0, %arg1 : i32
    return %0 : i1
  }
  func.func @main() -> i32 {
    %c40_i32 = arith.constant 40 : i32
    %c42_i32 = arith.constant 42 : i32
    %0 = call @less_than(%c40_i32, %c42_i32) : (i32, i32) -> i1
    %1 = scf.if %0 -> (i32) {
      %c1_i32 = arith.constant 1 : i32
      scf.yield %c1_i32 : i32
    } else {
      %c0_i32 = arith.constant 0 : i32
      scf.yield %c0_i32 : i32
    }
    return %1 : i32
  }
}
