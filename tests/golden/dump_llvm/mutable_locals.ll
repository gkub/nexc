; ModuleID = 'nex_module'
source_filename = "nex_module"

define i32 @main() {
  %1 = alloca i32, i64 1, align 4
  %2 = insertvalue { ptr, ptr, i64 } undef, ptr %1, 0
  %3 = insertvalue { ptr, ptr, i64 } %2, ptr %1, 1
  %4 = insertvalue { ptr, ptr, i64 } %3, i64 0, 2
  store i32 40, ptr %1, align 4
  %5 = alloca i32, i64 1, align 4
  %6 = insertvalue { ptr, ptr, i64 } undef, ptr %5, 0
  %7 = insertvalue { ptr, ptr, i64 } %6, ptr %5, 1
  %8 = insertvalue { ptr, ptr, i64 } %7, i64 0, 2
  store i32 2, ptr %5, align 4
  %9 = load i32, ptr %5, align 4
  %10 = load i32, ptr %1, align 4
  %11 = add i32 %9, %10
  store i32 %11, ptr %5, align 4
  %12 = load i32, ptr %5, align 4
  ret i32 %12
}
