//@ crate-type: lib
//@ compile-flags: --target $DIR/cov99_w1139_x86_register_class.toml -Zstop-after=expand
//@ input: cov99_w1139_x86_register_class.toml

#![feature(no_core, rustc_attrs)]
#![allow(internal_features)]
#![no_core]

#[rustc_builtin_macro]
macro_rules! asm {
    () => {};
}

pub unsafe fn classify_register() {
    asm!("/* {0} */", out(reg) _);
}
