#![feature(core_intrinsics)]

use std::intrinsics::discriminant_value;

fn main() {
    assert_eq!(discriminant_value(&10), 0u8);
}
