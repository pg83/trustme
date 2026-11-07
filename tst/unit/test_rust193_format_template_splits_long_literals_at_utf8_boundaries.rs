//@ crate-type: lib
//@ env: RUSTC_OVERRIDE_VERSION_STRING=1.93.0
//@ compile-flags: -Zstop-after=expand -Zdump-ast
//@ output-contains: _1_ast.rs b"\x80\xFC\xFF
//@ output-contains: _1_ast.rs \x04\xF0\x9F\xA6\x80\xC0\x00
#![no_core]
#![feature(no_core, rustc_attrs)]

#[rustc_builtin_macro]
macro_rules! concat { () => {} }
#[rustc_builtin_macro]
macro_rules! format_args { () => {} }
macro_rules! four {
    ($text:expr) => { concat!($text, $text, $text, $text) };
}

pub fn encoded(value: u16) {
    let _ = format_args!(concat!(
        four!(four!(four!(four!(four!("🦀🦀🦀🦀🦀🦀🦀🦀🦀🦀🦀🦀🦀🦀🦀🦀"))))), "{}"
    ), value);
}
