#![feature(core_intrinsics)]

use std::intrinsics::discriminant_value;

fn main() {
    let value: u8 = discriminant_value(&true);
    assert_eq!(value, 0);
}
