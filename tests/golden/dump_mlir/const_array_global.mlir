module {
  memref.global "private" constant @__nex_const_GRID : memref<2x2xi32> = dense<[[1, 2], [3, 4]]>
  func.func @main() -> i32 {
    %0 = memref.get_global @__nex_const_GRID : memref<2x2xi32>
    %alloca = memref.alloca() : memref<2x2xi32>
    %c0 = arith.constant 0 : index
    %c0_0 = arith.constant 0 : index
    %1 = memref.load %0[%c0, %c0_0] : memref<2x2xi32>
    memref.store %1, %alloca[%c0, %c0_0] : memref<2x2xi32>
    %c0_1 = arith.constant 0 : index
    %c1 = arith.constant 1 : index
    %2 = memref.load %0[%c0_1, %c1] : memref<2x2xi32>
    memref.store %2, %alloca[%c0_1, %c1] : memref<2x2xi32>
    %c1_2 = arith.constant 1 : index
    %c0_3 = arith.constant 0 : index
    %3 = memref.load %0[%c1_2, %c0_3] : memref<2x2xi32>
    memref.store %3, %alloca[%c1_2, %c0_3] : memref<2x2xi32>
    %c1_4 = arith.constant 1 : index
    %c1_5 = arith.constant 1 : index
    %4 = memref.load %0[%c1_4, %c1_5] : memref<2x2xi32>
    memref.store %4, %alloca[%c1_4, %c1_5] : memref<2x2xi32>
    %c0_i32 = arith.constant 0 : i32
    %5 = arith.index_cast %c0_i32 : i32 to index
    %subview = memref.subview %alloca[%5, 0] [1, 2] [1, 1] : memref<2x2xi32> to memref<2xi32, strided<[1], offset: ?>>
    %c1_i32 = arith.constant 1 : i32
    %6 = arith.index_cast %c1_i32 : i32 to index
    %7 = memref.load %subview[%6] : memref<2xi32, strided<[1], offset: ?>>
    %8 = memref.get_global @__nex_const_GRID : memref<2x2xi32>
    %c1_i32_6 = arith.constant 1 : i32
    %9 = arith.index_cast %c1_i32_6 : i32 to index
    %subview_7 = memref.subview %8[%9, 0] [1, 2] [1, 1] : memref<2x2xi32> to memref<2xi32, strided<[1], offset: ?>>
    %c0_i32_8 = arith.constant 0 : i32
    %10 = arith.index_cast %c0_i32_8 : i32 to index
    %11 = memref.load %subview_7[%10] : memref<2xi32, strided<[1], offset: ?>>
    %12 = arith.addi %7, %11 : i32
    %13 = memref.get_global @__nex_const_GRID : memref<2x2xi32>
    %c1_i32_9 = arith.constant 1 : i32
    %14 = arith.index_cast %c1_i32_9 : i32 to index
    %subview_10 = memref.subview %13[%14, 0] [1, 2] [1, 1] : memref<2x2xi32> to memref<2xi32, strided<[1], offset: ?>>
    %c1_i32_11 = arith.constant 1 : i32
    %15 = arith.index_cast %c1_i32_11 : i32 to index
    %16 = memref.load %subview_10[%15] : memref<2xi32, strided<[1], offset: ?>>
    %17 = arith.addi %12, %16 : i32
    return %17 : i32
  }
}
