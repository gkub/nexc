module {
  func.func @main() -> i32 {
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
      %1 = memref.load %alloca_1[] : memref<i32>
      %c10_i32 = arith.constant 10 : i32
      %2 = arith.cmpi slt, %1, %c10_i32 : i32
      %true = arith.constant true
      %3 = memref.load %alloca_2[] : memref<i1>
      %4 = arith.xori %3, %true : i1
      %5 = arith.andi %2, %4 : i1
      scf.condition(%5)
    } do {
      %1 = memref.load %alloca[] : memref<i32>
      %2 = memref.load %alloca_1[] : memref<i32>
      %3 = arith.addi %1, %2 : i32
      memref.store %3, %alloca[] : memref<i32>
      %4 = memref.load %alloca_1[] : memref<i32>
      %c1_i32 = arith.constant 1 : i32
      %5 = arith.addi %4, %c1_i32 : i32
      memref.store %5, %alloca_1[] : memref<i32>
      scf.yield
    }
    %0 = memref.load %alloca[] : memref<i32>
    return %0 : i32
  }
}
