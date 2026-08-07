#![feature(core_intrinsics)]

use std::intrinsics::discriminant_value;

enum Value {
    First = 4,
    Second = 9,
}

const DISCRIMINANT: isize = discriminant_value(&Value::Second);

fn main() {
    assert_eq!(DISCRIMINANT, 9);
}
