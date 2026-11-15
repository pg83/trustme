//@ compile-only
//@ compile-flags: -Z stop-after=typeck
#![feature(trivial_bounds)]
#![allow(dead_code, unreachable_code)]

fn pointer<T>(_: T) -> *const T {
    loop {}
}

fn expected_str() where str: Sized {
    let _: *const str = pointer(loop {});
}

fn main() {}
