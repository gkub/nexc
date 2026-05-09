; ModuleID = 'nex_module'
source_filename = "nex_module"

define i32 @sum_even_to(i32 %0) {
  %2 = alloca i32, i64 1, align 4
  %3 = insertvalue { ptr, ptr, i64 } undef, ptr %2, 0
  %4 = insertvalue { ptr, ptr, i64 } %3, ptr %2, 1
  %5 = insertvalue { ptr, ptr, i64 } %4, i64 0, 2
  store i32 0, ptr %2, align 4
  %6 = alloca i32, i64 1, align 4
  %7 = insertvalue { ptr, ptr, i64 } undef, ptr %6, 0
  %8 = insertvalue { ptr, ptr, i64 } %7, ptr %6, 1
  %9 = insertvalue { ptr, ptr, i64 } %8, i64 0, 2
  store i32 0, ptr %6, align 4
  br label %10

10:                                               ; preds = %23, %1
  %11 = load i32, ptr %2, align 4
  %12 = icmp sle i32 %11, %0
  br i1 %12, label %13, label %26

13:                                               ; preds = %10
  %14 = load i32, ptr %2, align 4
  %15 = srem i32 %14, 2
  %16 = icmp eq i32 %15, 0
  br i1 %16, label %17, label %21

17:                                               ; preds = %13
  %18 = load i32, ptr %6, align 4
  %19 = load i32, ptr %2, align 4
  %20 = add i32 %18, %19
  store i32 %20, ptr %6, align 4
  br label %23

21:                                               ; preds = %13
  %22 = load i32, ptr %6, align 4
  store i32 %22, ptr %6, align 4
  br label %23

23:                                               ; preds = %17, %21
  %24 = load i32, ptr %2, align 4
  %25 = add i32 %24, 1
  store i32 %25, ptr %2, align 4
  br label %10

26:                                               ; preds = %10
  %27 = load i32, ptr %6, align 4
  ret i32 %27
}

define i32 @main() {
  %1 = call i32 @sum_even_to(i32 10)
  ret i32 %1
}
