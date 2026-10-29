//@ aux-build: offset_item_exports.rs

use offset_item_exports::forwarded;
use std::mem::offset_of;

#[repr(C)]
struct Inner {
    tag: u8,
    value: u64,
}

#[repr(C)]
struct Outer {
    prefix: u16,
    inner: Inner,
}

#[repr(C)]
struct Generic<T> {
    first: u8,
    pair: (u16, T),
}

const NESTED: usize = offset_of!(Outer, inner.value);

fn generic_offset<T>() -> usize {
    offset_of!(Generic<T>, pair.1)
}

fn main() {
    assert_eq!(NESTED, offset_of!(Outer, inner) + offset_of!(Inner, value));
    assert_eq!(generic_offset::<u32>(), offset_of!(Generic<u32>, pair.1));
    assert_eq!(generic_offset::<u64>(), offset_of!(Generic<u64>, pair.1));
    assert_eq!(forwarded::add(NESTED), NESTED + forwarded::BASE);
    assert_eq!(offset_item_exports::add(5), 12);
}
