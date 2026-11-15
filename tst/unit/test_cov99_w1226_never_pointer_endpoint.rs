//@ compile-only
//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(never_type)]

trait Mark {}
impl Mark for ! {}

fn demand<T: Mark>(_: *const T) {}

pub fn route(pointer: *const !) {
    let inferred: *const _ = pointer;
    demand(inferred);
}
