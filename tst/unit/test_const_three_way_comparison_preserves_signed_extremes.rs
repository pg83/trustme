#![feature(core_intrinsics)]
use core::cmp::Ordering;
const LOW: Ordering = core::intrinsics::three_way_compare(i128::MIN, -19i128);
const HIGH: Ordering = core::intrinsics::three_way_compare(-3i8, -7i8);
const EQUAL: Ordering = core::intrinsics::three_way_compare(-41isize, -41isize);

fn main() {
    assert_eq!(LOW, Ordering::Less);
    assert_eq!(HIGH, Ordering::Greater);
    assert_eq!(EQUAL, Ordering::Equal);
}
