#![feature(rustc_attrs)]
#![allow(internal_features)]

#[rustc_builtin_macro]
macro_rules! llvm_asm {
    ($($tokens:tt)*) => {};
}

#[inline]
pub unsafe fn bump<T: Copy>(carried: T, input: u32) -> (T, u32) {
    let mut output = 0u32;
    llvm_asm!(
        "movl $1, $0; addl $$7, $0"
        : "=r"(output)
        : "r"(input)
        : "cc"
        : "volatile"
    );
    (carried, output)
}
