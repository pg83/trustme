//@ compile-fail: error
#![feature(derive_coerce_pointee)]
use std::marker::CoercePointee;
#[derive(CoercePointee)]
enum NotStruct<T> {
    Value(T),
}
fn main() {}
