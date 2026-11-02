#![feature(core_intrinsics)]
use core::cmp::Ordering;
const HIGH: Ordering = core::intrinsics::three_way_compare('\u{10000}', '\0');
const LOW: Ordering = core::intrinsics::three_way_compare('\0', '\u{10000}');
const EQUAL: Ordering = core::intrinsics::three_way_compare('\u{10ffff}', '\u{10ffff}');
const INTEGER: Ordering = core::intrinsics::three_way_compare(1u128 << 100, 17u128);

fn main() {
    assert_eq!(HIGH, Ordering::Greater);
    assert_eq!(LOW, Ordering::Less);
    assert_eq!(EQUAL, Ordering::Equal);
    assert_eq!(INTEGER, Ordering::Greater);
}
