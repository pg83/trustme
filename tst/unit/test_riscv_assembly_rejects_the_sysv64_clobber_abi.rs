//@ compile-fail: Unsupported clobber ABI
//@ compile-flags: --target=riscv64-unknown-linux-gnu
#![feature(no_core, rustc_attrs)]
#![no_core]

#[rustc_builtin_macro]
macro_rules! asm { () => {} }

fn main() {
    unsafe {
        asm!("", clobber_abi("sysv64"));
    }
}
