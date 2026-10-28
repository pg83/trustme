//@ compile-flags: -Zdump-ast -Zdump-hir -Zdump-mir
//@ output-contains: _1_ast.rs movq $2, $0; movq $3, $1
//@ output-contains: _2_hir.rs llvm_asm!()
//@ output-contains: _3_mir.rs movq
#![feature(rustc_attrs)]

#[rustc_builtin_macro]
macro_rules! llvm_asm { () => {} }

fn main() {
    let mut value: u64;
    let mut other: u64;
    unsafe {
        llvm_asm!("movq $2, $0; movq $3, $1"
            : "=&r"(value), "=r"(other)
            : "r"(17u64), "r"(29u64)
            : "memory", "cc"
            : "volatile", "alignstack");
        llvm_asm!("" ::: "memory" : "volatile");
        llvm_asm!("" ::::: volatile);
        llvm_asm!("");
        llvm_asm!("" :: "r"(value));
    }
    assert_eq!(value, 17);
    assert_eq!(other, 29);
}
