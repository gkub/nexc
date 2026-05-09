module {
  func.func @main() -> i32 {
    %c40_i32 = arith.constant 40 : i32
    %c2_i32 = arith.constant 2 : i32
    %2 = arith.addi %c40_i32, %c2_i32 : i32
    return %2 : i32
  }
}
