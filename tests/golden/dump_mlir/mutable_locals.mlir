module {
  func.func @main() -> i32 {
    %c40_i32 = arith.constant 40 : i32
    %alloca = memref.alloca() : memref<i32>
    memref.store %c40_i32, %alloca[] : memref<i32>
    %c2_i32 = arith.constant 2 : i32
    %alloca_0 = memref.alloca() : memref<i32>
    memref.store %c2_i32, %alloca_0[] : memref<i32>
    %0 = memref.load %alloca_0[] : memref<i32>
    %1 = memref.load %alloca[] : memref<i32>
    %2 = arith.addi %0, %1 : i32
    memref.store %2, %alloca_0[] : memref<i32>
    %3 = memref.load %alloca_0[] : memref<i32>
    return %3 : i32
  }
}
