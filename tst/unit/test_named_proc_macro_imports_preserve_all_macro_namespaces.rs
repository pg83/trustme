//@ proc-macro-aux-build: glob_macro_exports.rs
extern crate glob_macro_exports;
mod imported {
    pub use glob_macro_exports::{number as digits, passthrough as identity, Marked as Tagged};
}
use imported::{digits as amount, identity as keep, Tagged as Label};
#[derive(Label)]
struct Record(u32);
#[keep]
fn read() -> u32 { amount!() }
fn main() { assert_eq!(Record(read()).0, 83); }
