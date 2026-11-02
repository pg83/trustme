//@ aux-build: delegated_pattern_signatures.rs
//@ compile-variant: -O
#![feature(fn_delegation, pattern_types, pattern_type_macro)]
#![allow(incomplete_features)]
use delegated_pattern_signatures::{Patterns, Source, Wide};
struct Wrapped(Source);
impl Patterns for Wrapped { reuse Patterns::wide { self.0 } }
fn main() {
    let value: Wide = unsafe { std::mem::transmute(1u128 << 100) };
    assert_eq!(Wrapped(Source).wide(value), 1u128 << 100);
}
