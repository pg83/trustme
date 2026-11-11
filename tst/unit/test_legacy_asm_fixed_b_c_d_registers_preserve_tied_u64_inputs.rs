#![feature(rustc_attrs)]
#![allow(internal_features)]

#[rustc_builtin_macro]
macro_rules! llvm_asm {}

fn main() {
    let mut b = 0u64;
    let mut c = 0u64;
    let mut d = 0u64;
    unsafe {
        llvm_asm!(
            "addq $$5, $0; xorq $$7, $1; subq $$11, $2"
            : "={rbx}"(b), "={rcx}"(c), "={rdx}"(d)
            : "0"(13u64), "1"(29u64), "2"(47u64)
            : "cc"
            : "volatile"
        );
    }
    assert_eq!((b, c, d), (18, 26, 36));
}
