//@ compile-fail: error
#![feature(async_trait_bounds)]

trait Item {}

fn require(_: impl async Item) {}

fn main() {}
