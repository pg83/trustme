//@ aux-build: delegated_semantic_signatures.rs
//@ compile-variant: -O
#![feature(fn_delegation, pattern_types, pattern_type_macro)]
#![allow(incomplete_features)]
use delegated_semantic_signatures::{Signatures, Source};
struct Wrapped(Source);
impl Signatures for Wrapped { reuse Signatures::bounded { self.0 } }
fn main() {
    let wrapped = Wrapped(Source);
    assert_eq!(wrapped.bounded(3), 3);
    assert_eq!(wrapped.bounded(9), 9);
}
