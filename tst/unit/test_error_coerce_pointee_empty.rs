//@ compile-fail: error
#![feature(derive_coerce_pointee)]
use std::marker::CoercePointee;
#[derive(CoercePointee)]
struct Empty;
fn main() {}
