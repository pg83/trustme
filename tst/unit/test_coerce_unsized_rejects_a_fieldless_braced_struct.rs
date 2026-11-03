//@ compile-fail: error:
#![feature(coerce_unsized, unsize)]
use std::ops::CoerceUnsized;
use std::marker::{PhantomData, Unsize};
struct Empty {}
impl CoerceUnsized<Empty> for Empty {}
fn main() {}
