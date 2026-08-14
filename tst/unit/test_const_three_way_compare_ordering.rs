#![feature(core_intrinsics)]
#![allow(internal_features)]

use core::cmp::Ordering;

const LESS: Ordering = core::intrinsics::three_way_compare(1u8, 2u8);
const EQUAL: Ordering = core::intrinsics::three_way_compare(2u8, 2u8);
const GREATER: Ordering = core::intrinsics::three_way_compare(3u8, 2u8);

fn main() {
    assert_eq!(LESS, Ordering::Less);
    assert_eq!(EQUAL, Ordering::Equal);
    assert_eq!(GREATER, Ordering::Greater);
}
