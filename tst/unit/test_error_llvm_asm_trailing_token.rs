//@ compile-fail: Unexpected token in asm!
#![feature(rustc_attrs)]
#[rustc_builtin_macro]
macro_rules! llvm_asm { () => {} }
fn main() { unsafe { llvm_asm!("" unexpected); } }
