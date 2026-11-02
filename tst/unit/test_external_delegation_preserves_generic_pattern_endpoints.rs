//@ aux-build: delegated_pattern_signatures.rs
//@ compile-variant: -O
#![feature(fn_delegation, pattern_types, pattern_type_macro, generic_pattern_types)]
#![feature(generic_const_exprs, const_trait_impl, pattern_type_range_trait)]
#![allow(incomplete_features)]
use delegated_pattern_signatures::{Patterns, Source};
struct Wrapped(Source);
impl Patterns for Wrapped { reuse Patterns::window { self.0 } }
fn main() {
    assert_eq!(Wrapped(Source).window::<3, 9>(3), 3);
    assert_eq!(Wrapped(Source).window::<3, 9>(8), 8);
}
