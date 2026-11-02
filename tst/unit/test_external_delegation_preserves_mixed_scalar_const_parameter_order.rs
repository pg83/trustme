//@ aux-build: delegated_scalar_signatures.rs
//@ compile-variant: -O
#![feature(fn_delegation)]
#![allow(incomplete_features)]
use delegated_scalar_signatures::{Scalars, Source, Token};
struct Wrapped(Source);
impl Scalars for Wrapped { reuse Scalars::map { self.0 } }
fn main() {
    let output: Token<false, u16, '🦀', -9, { 1u128 << 110 }> = Wrapped(Source).map(Token(37));
    assert_eq!(output.0, 40);
}
