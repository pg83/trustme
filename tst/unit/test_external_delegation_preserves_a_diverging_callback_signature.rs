//@ aux-build: delegated_semantic_signatures.rs
//@ compile-variant: -O
#![feature(fn_delegation)]
#![allow(incomplete_features)]
use delegated_semantic_signatures::{Signatures, Source};
struct Wrapped(Source);
impl Signatures for Wrapped { reuse Signatures::diverging { self.0 } }
fn main() {
    let callback: fn(&Wrapped, fn() -> !) -> ! = <Wrapped as Signatures>::diverging;
    std::hint::black_box(callback);
    assert_eq!(std::mem::size_of::<Wrapped>(), 0);
}
