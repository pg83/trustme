//@ compile-fail: W704Required
//@ compile-flags: -Z stop-after=typeck
#![feature(unsize)]

use std::marker::Unsize;

trait W704Required {}
struct Missing;

fn demand<T: Unsize<dyn W704Required + Send>>() {}

fn main() {
    demand::<Missing>();
}
