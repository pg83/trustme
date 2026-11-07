#![feature(intrinsics, rustc_attrs)]
#![allow(internal_features)]

#[rustc_intrinsic]
const fn offset_of<T>(variant: u32, field: u32) -> usize;

#[repr(C)]
struct Inner {
    first: u16,
    second: u32,
}

#[repr(C)]
struct Outer {
    tag: u8,
    inner: Inner,
}

const INNER_OFFSET: usize = offset_of::<Outer>(0, 1);

fn main() {
    let second_offset = std::hint::black_box(offset_of::<Inner>(0, 1));
    assert_eq!(INNER_OFFSET, std::mem::offset_of!(Outer, inner));
    assert_eq!(second_offset, std::mem::offset_of!(Inner, second));
    assert_eq!(std::mem::offset_of!(Outer, inner.second), INNER_OFFSET + second_offset);
}
