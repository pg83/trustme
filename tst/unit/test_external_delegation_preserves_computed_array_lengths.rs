//@ aux-build: delegated_expression_arrays.rs
//@ compile-variant: -O
#![feature(fn_delegation, generic_const_exprs)]
#![allow(incomplete_features)]

use delegated_expression_arrays::{Source, Windows};
struct Wrapped(Source);
impl Windows for Wrapped { reuse Windows::unchanged { self.0 } }

fn main() {
    assert_eq!(Wrapped(Source).unchanged::<2>([227, 229, 233]), [227, 229, 233]);
}
