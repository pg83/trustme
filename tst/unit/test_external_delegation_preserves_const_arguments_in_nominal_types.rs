//@ aux-build: delegated_semantic_signatures.rs
//@ compile-variant: -O
#![feature(fn_delegation)]
#![allow(incomplete_features)]
use delegated_semantic_signatures::{Signatures, Source};
struct Wrapped(Source);
impl Signatures for Wrapped { reuse Signatures::count { self.0 } }
fn main() {
    let wrapped = Wrapped(Source);
    assert_eq!(wrapped.count([7u8, 11, 13].into_iter()), 3);
    assert_eq!(wrapped.count([17u8; 0].into_iter()), 0);
}
