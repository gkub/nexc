module {
  llvm.mlir.global private constant @__nex_fmt_main_1("\0A") {addr_space = 0 : i32}
  func.func private @nex_runtime_print_i64(i64)
  func.func private @nex_runtime_print_str(!llvm.ptr, i64)
  llvm.mlir.global private constant @__nex_fmt_main_0("sum = ") {addr_space = 0 : i32}
  llvm.mlir.global private constant @__nex_str_main_8("sum = {}\0A") {addr_space = 0 : i32}
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
      %0 = memref.load %alloca_1[] : memref<i32>
      %c10_i32 = arith.constant 10 : i32
      %1 = arith.cmpi slt, %0, %c10_i32 : i32
      %true = arith.constant true
      %2 = memref.load %alloca_2[] : memref<i1>
      %3 = arith.xori %2, %true : i1
      %4 = arith.andi %1, %3 : i1
      scf.condition(%4)
    } do {
      %0 = memref.load %alloca[] : memref<i32>
      %1 = memref.load %alloca_1[] : memref<i32>
      %2 = arith.addi %0, %1 : i32
      memref.store %2, %alloca[] : memref<i32>
      %3 = llvm.mlir.addressof @__nex_str_main_8 : !llvm.ptr
      %c9_i64 = arith.constant 9 : i64
      %4 = memref.load %alloca[] : memref<i32>
      %5 = llvm.mlir.addressof @__nex_fmt_main_0 : !llvm.ptr
      %c6_i64 = arith.constant 6 : i64
      func.call @nex_runtime_print_str(%5, %c6_i64) : (!llvm.ptr, i64) -> ()
      %6 = arith.extsi %4 : i32 to i64
      func.call @nex_runtime_print_i64(%6) : (i64) -> ()
      %7 = llvm.mlir.addressof @__nex_fmt_main_1 : !llvm.ptr
      %c1_i64 = arith.constant 1 : i64
      func.call @nex_runtime_print_str(%7, %c1_i64) : (!llvm.ptr, i64) -> ()
      %8 = memref.load %alloca_1[] : memref<i32>
      %c1_i32 = arith.constant 1 : i32
      %9 = arith.addi %8, %c1_i32 : i32
      memref.store %9, %alloca_1[] : memref<i32>
      scf.yield
    }
    %c0_i32_3 = arith.constant 0 : i32
    return %c0_i32_3 : i32
  }
}
