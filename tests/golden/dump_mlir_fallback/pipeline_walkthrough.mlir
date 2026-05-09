module {
  func.func @sum_even_to(%arg0: i32) -> i32 {
    %c0_i32 = arith.constant 0 : i32
    %0 = memref.alloca() : memref<i32>
    memref.store %c0_i32, %0[] : memref<i32>
    %c0_i32 = arith.constant 0 : i32
    %1 = memref.alloca() : memref<i32>
    memref.store %c0_i32, %1[] : memref<i32>
    scf.while : () -> () {
      %2 = memref.load %0[] : memref<i32>
      %3 = arith.cmpi sle, %2, %arg0 : i32
      scf.condition(%3)
    } do {
      %4 = memref.load %0[] : memref<i32>
      %c2_i32 = arith.constant 2 : i32
      %5 = arith.remsi %4, %c2_i32 : i32
      %c0_i32 = arith.constant 0 : i32
      %6 = arith.cmpi eq, %5, %c0_i32 : i32
      scf.if %6 {
        %7 = memref.load %1[] : memref<i32>
        %8 = memref.load %0[] : memref<i32>
        %9 = arith.addi %7, %8 : i32
        memref.store %9, %1[] : memref<i32>
      } else {
        %10 = memref.load %1[] : memref<i32>
        memref.store %10, %1[] : memref<i32>
      }
      %11 = memref.load %0[] : memref<i32>
      %c1_i32 = arith.constant 1 : i32
      %12 = arith.addi %11, %c1_i32 : i32
      memref.store %12, %0[] : memref<i32>
      scf.yield
    }
    %13 = memref.load %1[] : memref<i32>
    return %13 : i32
  }
  func.func @main() -> i32 {
    %c10_i32 = arith.constant 10 : i32
    %0 = call @sum_even_to(%c10_i32) : (i32) -> i32
    return %0 : i32
  }
}
