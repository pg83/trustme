// core's `NonZeroCharInner(char as u32 in 1..=0x10ffff)`: the valid-range
// attributes narrow the range of the field's own scalar, here a `char`'s
// `0..=0x10ffff`, so `None` of `Option<NonZero<char>>` is zero. Starting from
// the full `u32` range left the attributes a niche of one value, and the
// `char`'s own niche put `None` at 0x110000: `NonZero::<char>::new('\0')`
// came back `Some`.
#![feature(rustc_attrs)]
#![allow(internal_features)]
use core::num::NonZero;
use std::mem::size_of;

#[rustc_layout_scalar_valid_range_start(1)]
#[rustc_layout_scalar_valid_range_end(0x10ffff)]
#[derive(Clone, Copy)]
struct Inner(char);

fn bytes<T>(value: &T) -> Vec<u8> {
    let start = value as *const T as *const u8;
    (0..size_of::<T>()).map(|i| unsafe { *start.add(i) }).collect()
}

fn main() {
    assert_eq!(bytes(&None::<Inner>), [0, 0, 0, 0]);
    assert_eq!(NonZero::<char>::new(0 as char), None);
    assert_eq!(bytes(&None::<NonZero<char>>), [0, 0, 0, 0]);
    assert!(NonZero::<char>::new(char::MAX).is_some());
}
