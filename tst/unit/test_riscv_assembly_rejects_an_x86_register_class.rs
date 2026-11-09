//@ compile-fail: Unknown register for riscv64
//@ compile-flags: --target=riscv64-unknown-linux-gnu
#![feature(no_core, rustc_attrs)]
#![no_core]

#[rustc_builtin_macro]
macro_rules! asm { () => {} }

fn main() {
    unsafe {
        asm!("/* {value} */", value = in(xmm_reg) 1u64);
    }
}
