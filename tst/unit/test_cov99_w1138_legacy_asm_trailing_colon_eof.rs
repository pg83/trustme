//@ compile-only
//@ compile-flags: -Zunpretty=expanded
#![feature(rustc_attrs)]

#[rustc_builtin_macro]
macro_rules! llvm_asm { () => {} }

fn main() {
    unsafe {
        llvm_asm!("" : : : : :);
    }
}
