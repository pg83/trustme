//@ compile-fail: iter! requires a plain closure
#![feature(no_core, rustc_attrs)]
#![no_core]

#[rustc_builtin_macro]
macro_rules! iter { () => {} }

fn main() {
    let _ = iter!(233u8);
}
