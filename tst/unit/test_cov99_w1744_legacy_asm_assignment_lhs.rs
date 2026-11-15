//@ compile-fail: invalid left-hand side of assignment
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(rustc_attrs)]

#[rustc_builtin_macro]
macro_rules! llvm_asm {
    () => {};
}

fn main() {
    (llvm_asm!("")) = ();
}
