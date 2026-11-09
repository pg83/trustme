//@ compile-fail: Delegation glob target is not a trait
#![feature(fn_delegation)]

struct Holder;
struct NotATrait;

impl Holder {
    reuse <() as NotATrait>::*;
}

fn main() {}
