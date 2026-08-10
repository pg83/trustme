#![feature(offset_of_slice)]

use std::mem::{offset_of, size_of};

#[repr(C)]
struct Outer {
    tag: u8,
    pair: (u8, u16),
}

#[repr(C)]
struct Unsized {
    tag: u8,
    tail: [u32],
}

#[repr(C)]
struct NestedUnsized {
    prefix: u32,
    inner: Unsized,
}

type ComplexTuple = ((u8, (u8, (u8, u16), u8)), (u8, u32, u16));

fn main() {
    assert!(offset_of!((u8, u16), 0) < size_of::<(u8, u16)>());

    let value = Outer { tag: 0, pair: (0, 0) };
    let base = &value as *const Outer as usize;
    let field = &value.pair.1 as *const u16 as usize;
    assert_eq!(offset_of!(Outer, pair.1), field - base);

    let compact = offset_of!(ComplexTuple, 0.1.1.1);
    assert_eq!(compact, offset_of!(ComplexTuple, 0. 1.1.1));
    assert_eq!(compact, offset_of!(ComplexTuple, 0 . 1.1.1));
    assert_eq!(compact, offset_of!(ComplexTuple, 0 .1.1.1));
    assert_eq!(compact, offset_of!(ComplexTuple, 0.1 .1.1));
    assert_eq!(compact, offset_of!(ComplexTuple, 0.1 . 1.1));
    assert_eq!(compact, offset_of!(ComplexTuple, 0.1. 1.1));
    assert_eq!(compact, offset_of!(ComplexTuple, 0.1.1. 1));
    assert_eq!(compact, offset_of!(ComplexTuple, 0.1.1 . 1));
    assert_eq!(compact, offset_of!(ComplexTuple, 0.1.1 .1));
    assert!(offset_of!(ComplexTuple, 1.2) < size_of::<ComplexTuple>());

    assert_eq!(offset_of!(Unsized, tail), 4);
    assert_eq!(offset_of!(NestedUnsized, inner.tail), 8);
}
