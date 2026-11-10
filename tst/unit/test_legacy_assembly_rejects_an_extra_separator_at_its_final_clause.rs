#![feature(rustc_attrs)]
//@ compile-fail: Unexpected token in asm! - TOK_COLON

#[rustc_builtin_macro]
macro_rules! llvm_asm {
    () => {};
}

fn main() {
    unsafe {
        llvm_asm!("" : : : : ::);
    }
}
