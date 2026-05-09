module {
  func.func @main() -> i32 {
    %true = arith.constant true
    %0 = scf.if %true -> (i32) {
      %c1_i32 = arith.constant 1 : i32
      scf.yield %c1_i32 : i32
    } else {
      %c0_i32 = arith.constant 0 : i32
      scf.yield %c0_i32 : i32
    }
    return %0 : i32
  }
}
