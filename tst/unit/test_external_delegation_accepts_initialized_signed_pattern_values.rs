//@ aux-build: delegated_pattern_signatures.rs
//@ compile-variant: -O
#![feature(fn_delegation, pattern_types, pattern_type_macro)]
#![allow(incomplete_features)]
use delegated_pattern_signatures::{Patterns, Source, Split};
struct Wrapped(Source);
impl Patterns for Wrapped { reuse Patterns::split { self.0 } }
fn main() {
    let negative: Split = unsafe { std::mem::transmute(-7i16) };
    let positive: Split = unsafe { std::mem::transmute(5i16) };
    assert_eq!(Wrapped(Source).split(negative), -7);
    assert_eq!(Wrapped(Source).split(positive), 5);
}
