//@ aux-build: delegated_scalar_signatures.rs
//@ compile-variant: -O
#![feature(fn_delegation)]
#![allow(incomplete_features)]
use delegated_scalar_signatures::{Scalars, Source, Choice, Bits};
struct Wrapped(Source);
impl Scalars for Wrapped { reuse Scalars::{choice, bits, flag} { self.0 } }
fn main() {
    let wrapped = Wrapped(Source);
    assert_eq!(wrapped.choice(Choice::Bytes([5, 7])), 12);
    assert_eq!(wrapped.choice(Choice::Empty), 0);
    assert_eq!(wrapped.bits(Bits { bytes: [9, 1, 0, 0, 0, 0, 0, 0] }), 265);
    assert!(wrapped.flag(17u8));
}
