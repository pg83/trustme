//@ aux-build: delegated_nonnull_signature.rs
//@ compile-variant: -O
#![feature(fn_delegation, pattern_types, pattern_type_macro)]
#![allow(incomplete_features)]
use delegated_nonnull_signature::{Present, Read, Source};
struct Wrapped(Source);
impl Read for Wrapped { reuse Read::read { self.0 } }
fn main() {
    let value = 53u32;
    let pointer: Present = unsafe { std::mem::transmute(&value as *const u32) };
    assert_eq!(Wrapped(Source).read(pointer), 53);
}
