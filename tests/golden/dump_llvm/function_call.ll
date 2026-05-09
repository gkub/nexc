; ModuleID = 'nex_module'
source_filename = "nex_module"

define i32 @add(i32 %0, i32 %1) {
  %3 = add i32 %0, %1
  ret i32 %3
}

define i32 @main() {
  %1 = call i32 @add(i32 40, i32 2)
  ret i32 %1
}
