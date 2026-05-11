module {
  func.func @sum_even_to(%arg0: i32) -> i32 {
    %c0_i32 = arith.constant 0 : i32
    %0 = memref.alloca() : memref<i32>
    memref.store %c0_i32, %0[] : memref<i32>
    %c0_i32_0 = arith.constant 0 : i32
    %1 = memref.alloca() : memref<i32>
    memref.store %c0_i32_0, %1[] : memref<i32>
    %2 = memref.alloca() : memref<i1>
    %3 = arith.constant false
    memref.store %3, %2[] : memref<i1>
    scf.while : () -> () {
      %4 = memref.load %0[] : memref<i32>
      %5 = arith.cmpi sle, %4, %arg0 : i32
      %6 = arith.constant true
      %7 = memref.load %2[] : memref<i1>
      %8 = arith.xori %7, %6 : i1
      %9 = arith.andi %5, %8 : i1
      scf.condition(%9)
    } do {
      %10 = memref.load %0[] : memref<i32>
      %c2_i32 = arith.constant 2 : i32
      %11 = arith.remsi %10, %c2_i32 : i32
      %c0_i32_1 = arith.constant 0 : i32
      %12 = arith.cmpi eq, %11, %c0_i32_1 : i32
      scf.if %12 {
        %13 = memref.load %1[] : memref<i32>
        %14 = memref.load %0[] : memref<i32>
        %15 = arith.addi %13, %14 : i32
        memref.store %15, %1[] : memref<i32>
      } else {
        %16 = memref.load %1[] : memref<i32>
        memref.store %16, %1[] : memref<i32>
      }
      %17 = memref.load %0[] : memref<i32>
      %c1_i32 = arith.constant 1 : i32
      %18 = arith.addi %17, %c1_i32 : i32
      memref.store %18, %0[] : memref<i32>
      scf.yield
    }
    %19 = memref.load %1[] : memref<i32>
    return %19 : i32
  }
  func.func @main() -> i32 {
    %c10_i32 = arith.constant 10 : i32
    %0 = call @sum_even_to(%c10_i32) : (i32) -> i32
    return %0 : i32
  }
}
