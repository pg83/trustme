//@ compile-fail: Empty glob delegation
#![feature(fn_delegation)]

trait Empty {}
struct Holder;

impl Holder {
    reuse Empty::*;
}

fn main() {}
