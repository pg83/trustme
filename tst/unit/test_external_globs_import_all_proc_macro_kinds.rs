//@ proc-macro-aux-build: glob_macro_exports.rs
extern crate glob_macro_exports;
use glob_macro_exports::*;
#[derive(Marked)]
struct Item(u32);
#[passthrough]
fn value() -> u32 { number!() }
fn main() { assert_eq!(Item(value()).0, 83); }
