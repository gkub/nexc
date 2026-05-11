; ModuleID = 'nex_module'
source_filename = "nex_module"

define i32 @main() {
  %1 = alloca i32, i64 1, align 4
  %2 = insertvalue { ptr, ptr, i64 } undef, ptr %1, 0
  %3 = insertvalue { ptr, ptr, i64 } %2, ptr %1, 1
  %4 = insertvalue { ptr, ptr, i64 } %3, i64 0, 2
  store i32 0, ptr %1, align 4
  %5 = alloca i32, i64 1, align 4
  %6 = insertvalue { ptr, ptr, i64 } undef, ptr %5, 0
  %7 = insertvalue { ptr, ptr, i64 } %6, ptr %5, 1
  %8 = insertvalue { ptr, ptr, i64 } %7, i64 0, 2
  store i32 0, ptr %5, align 4
  %9 = alloca i1, i64 1, align 1
  %10 = insertvalue { ptr, ptr, i64 } undef, ptr %9, 0
  %11 = insertvalue { ptr, ptr, i64 } %10, ptr %9, 1
  %12 = insertvalue { ptr, ptr, i64 } %11, i64 0, 2
  store i1 false, ptr %9, align 1
  br label %13

13:                                               ; preds = %19, %0
  %14 = load i32, ptr %5, align 4
  %15 = icmp slt i32 %14, 10
  %16 = load i1, ptr %9, align 1
  %17 = xor i1 %16, true
  %18 = and i1 %15, %17
  br i1 %18, label %19, label %25

19:                                               ; preds = %13
  %20 = load i32, ptr %1, align 4
  %21 = load i32, ptr %5, align 4
  %22 = add i32 %20, %21
  store i32 %22, ptr %1, align 4
  %23 = load i32, ptr %5, align 4
  %24 = add i32 %23, 1
  store i32 %24, ptr %5, align 4
  br label %13

25:                                               ; preds = %13
  %26 = load i32, ptr %1, align 4
  ret i32 %26
}
