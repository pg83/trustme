//@ compile-fail: Unknown architecture for asm!
//@ compile-flags: --target=aarch64-unknown-linux-gnu
#![feature(no_core, rustc_attrs)]
#![no_core]

#[rustc_builtin_macro]
macro_rules! asm { () => {} }

fn main() {
    unsafe {
        asm!("/* {value} */", value = in(reg) 17u64);
    }
}
