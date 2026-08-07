#![feature(core_intrinsics)]

use std::intrinsics::discriminant_value;

#[repr(u128)]
enum Value {
    Zero = 0,
    High = u64::MAX as u128 + 1,
}

fn main() {
    let mut actual = [0u128; 2];
    actual[0] = discriminant_value(&Value::Zero);
    actual[1] = discriminant_value(&Value::High);
    assert_eq!(actual, [0, u64::MAX as u128 + 1]);
}
