//@ compile-fail: Associated type bounds on trait alias without a data trait
#![feature(trait_alias)]

trait Marker = Send;

fn rejected(_: &(dyn Marker<Item = u8>)) {}

fn main() {}
