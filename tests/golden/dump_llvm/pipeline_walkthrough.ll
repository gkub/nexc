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
  %10 = alloca i1, i64 1, align 1
  %11 = insertvalue { ptr, ptr, i64 } undef, ptr %10, 0
  %12 = insertvalue { ptr, ptr, i64 } %11, ptr %10, 1
  %13 = insertvalue { ptr, ptr, i64 } %12, i64 0, 2
  store i1 false, ptr %10, align 1
  br label %14

14:                                               ; preds = %30, %1
  %15 = load i32, ptr %2, align 4
  %16 = icmp sle i32 %15, %0
  %17 = load i1, ptr %10, align 1
  %18 = xor i1 %17, true
  %19 = and i1 %16, %18
  br i1 %19, label %20, label %33

20:                                               ; preds = %14
  %21 = load i32, ptr %2, align 4
  %22 = srem i32 %21, 2
  %23 = icmp eq i32 %22, 0
  br i1 %23, label %24, label %28

24:                                               ; preds = %20
  %25 = load i32, ptr %6, align 4
  %26 = load i32, ptr %2, align 4
  %27 = add i32 %25, %26
  store i32 %27, ptr %6, align 4
  br label %30

28:                                               ; preds = %20
  %29 = load i32, ptr %6, align 4
  store i32 %29, ptr %6, align 4
  br label %30

30:                                               ; preds = %24, %28
  %31 = load i32, ptr %2, align 4
  %32 = add i32 %31, 1
  store i32 %32, ptr %2, align 4
  br label %14

33:                                               ; preds = %14
  %34 = load i32, ptr %6, align 4
  ret i32 %34
}

define i32 @main() {
  %1 = call i32 @sum_even_to(i32 10)
  ret i32 %1
}
