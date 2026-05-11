module {
  func.func @sum_even_to(%arg0: i32) -> i32 {
    %c0_i32 = arith.constant 0 : i32
    %alloca = memref.alloca() : memref<i32>
    memref.store %c0_i32, %alloca[] : memref<i32>
    %c0_i32_0 = arith.constant 0 : i32
    %alloca_1 = memref.alloca() : memref<i32>
    memref.store %c0_i32_0, %alloca_1[] : memref<i32>
    %alloca_2 = memref.alloca() : memref<i1>
    %false = arith.constant false
    memref.store %false, %alloca_2[] : memref<i1>
    scf.while : () -> () {
      %1 = memref.load %alloca[] : memref<i32>
      %2 = arith.cmpi sle, %1, %arg0 : i32
      %true = arith.constant true
      %3 = memref.load %alloca_2[] : memref<i1>
      %4 = arith.xori %3, %true : i1
      %5 = arith.andi %2, %4 : i1
      scf.condition(%5)
    } do {
      %1 = memref.load %alloca[] : memref<i32>
      %c2_i32 = arith.constant 2 : i32
      %2 = arith.remsi %1, %c2_i32 : i32
      %c0_i32_3 = arith.constant 0 : i32
      %3 = arith.cmpi eq, %2, %c0_i32_3 : i32
      scf.if %3 {
        %6 = memref.load %alloca_1[] : memref<i32>
        %7 = memref.load %alloca[] : memref<i32>
        %8 = arith.addi %6, %7 : i32
        memref.store %8, %alloca_1[] : memref<i32>
      } else {
        %6 = memref.load %alloca_1[] : memref<i32>
        memref.store %6, %alloca_1[] : memref<i32>
      }
      %4 = memref.load %alloca[] : memref<i32>
      %c1_i32 = arith.constant 1 : i32
      %5 = arith.addi %4, %c1_i32 : i32
      memref.store %5, %alloca[] : memref<i32>
      scf.yield
    }
    %0 = memref.load %alloca_1[] : memref<i32>
    return %0 : i32
  }
  func.func @main() -> i32 {
    %c10_i32 = arith.constant 10 : i32
    %0 = call @sum_even_to(%c10_i32) : (i32) -> i32
    return %0 : i32
  }
}
