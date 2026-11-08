//@ compile-fail: Associated type bounds on auto trait
#![feature(auto_traits)]

auto trait Marker {}

fn rejected(_: &(dyn Marker<Item = u8>)) {}

fn main() {}
