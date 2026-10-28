//@ compile-fail: error
#![feature(derive_coerce_pointee)]
use std::marker::CoercePointee;
#[derive(CoercePointee)]
struct Multi<'a, #[pointee] T, #[pointee] U>(&'a T, &'a U);
fn main() {}
