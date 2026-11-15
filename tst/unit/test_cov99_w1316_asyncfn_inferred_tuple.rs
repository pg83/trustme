//@ compile-only
//@ compile-flags: --emit=metadata
#![feature(unboxed_closures, tuple_trait)]

use std::marker::Tuple;
use std::ops::AsyncFnOnce;

fn require<F, Args: Tuple>(_: F)
where
    F: AsyncFnOnce<Args>,
{}

async fn leaf() {}

fn main() {
    require(leaf);
}
