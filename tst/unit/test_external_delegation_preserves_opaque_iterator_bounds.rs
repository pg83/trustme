//@ aux-build: delegated_semantic_signatures.rs
//@ compile-variant: -O
#![feature(fn_delegation)]
#![allow(incomplete_features)]
use delegated_semantic_signatures::{Signatures, Source};
struct Wrapped(Source);
impl Signatures for Wrapped { reuse Signatures::values { self.0 } }
fn main() {
    let values = Wrapped(Source).values(19);
    assert_eq!(values.clone().sum::<u8>(), 39);
    assert_eq!(values.collect::<Vec<_>>(), [19, 20]);
}
