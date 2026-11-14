//@ compile-fail: Tuple
//@ compile-flags: -Z stop-after=typeck
#![feature(tuple_trait)]

fn tuple_value<T: core::marker::Tuple>() -> T {
    loop {}
}

fn main() {
    let value = tuple_value();
    let _: bool = value;
}
