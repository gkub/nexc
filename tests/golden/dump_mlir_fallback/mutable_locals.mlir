module {
  func.func @main() -> i32 {
    %c40_i32 = arith.constant 40 : i32
    %0 = memref.alloca() : memref<i32>
    memref.store %c40_i32, %0[] : memref<i32>
    %c2_i32 = arith.constant 2 : i32
    %1 = memref.alloca() : memref<i32>
    memref.store %c2_i32, %1[] : memref<i32>
    %2 = memref.load %1[] : memref<i32>
    %3 = memref.load %0[] : memref<i32>
    %4 = arith.addi %2, %3 : i32
    memref.store %4, %1[] : memref<i32>
    %5 = memref.load %1[] : memref<i32>
    return %5 : i32
  }
}
