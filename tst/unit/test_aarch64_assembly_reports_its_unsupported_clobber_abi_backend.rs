//@ compile-fail: clobber_abi is unsupported for target architecture `aarch64`
//@ compile-flags: --target=aarch64-unknown-linux-gnu
#![feature(no_core, rustc_attrs)]
#![no_core]

#[rustc_builtin_macro]
macro_rules! asm { () => {} }

fn main() {
    unsafe {
        asm!("", clobber_abi("C"));
    }
}
