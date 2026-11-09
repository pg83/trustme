//@ compile-fail: error
#![feature(async_trait_bounds)]

trait Item {}

fn require<T: async Item>() {}

fn main() {}
