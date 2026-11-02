//@ aux-build: delegated_pattern_signatures.rs
//@ compile-fail: type annotations needed: cannot infer
#![feature(fn_delegation, pattern_types, pattern_type_macro)]
#![allow(incomplete_features)]
use delegated_pattern_signatures::{Patterns, Source};
struct Wrapped(Source);
impl Patterns for Wrapped { reuse Patterns::split { self.0 } }
fn main() {
    assert_eq!(Wrapped(Source).split(-7), -7);
    assert_eq!(Wrapped(Source).split(5), 5);
}
