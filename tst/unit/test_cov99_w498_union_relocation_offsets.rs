//@ compile-fail: Value mismatch between
#![feature(adt_const_params)]
#![allow(incomplete_features, dead_code)]

#[derive(Copy, Clone)]
#[repr(C)]
struct Left {
    pointer: &'static u8,
    bits: usize,
}

#[derive(Copy, Clone)]
#[repr(C)]
struct Right {
    bits: usize,
    pointer: &'static u8,
}

union Value {
    left: Left,
    right: Right,
}

const FIRST: Value = Value { left: Left { pointer: &7, bits: 0x1000 } };
const SECOND: Value = Value { right: Right { bits: 0x1000, pointer: &7 } };

struct Key<const VALUE: Value>;
fn require_first(_: Key<FIRST>) {}

fn main() {
    require_first(Key::<SECOND>);
}
