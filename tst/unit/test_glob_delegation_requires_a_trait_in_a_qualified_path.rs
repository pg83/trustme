//@ compile-fail: Qualified path without a trait
#![feature(fn_delegation)]

struct Holder;

impl Holder {
    reuse <()>::*;
}

fn main() {}
