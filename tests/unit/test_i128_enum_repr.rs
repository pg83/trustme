#![feature(core_intrinsics)]

use std::intrinsics::discriminant_value;

#[repr(i128)]
enum Signed {
    Positive = 0x1223_3445_5667_7889,
    Negative = -0x1223_3445_5667_7889,
}

#[repr(u128)]
enum Unsigned {
    Value = 0x9876_5432_10,
}

fn main() {
    assert_eq!(std::mem::size_of::<Signed>(), 16);
    assert_eq!(std::mem::size_of::<Unsigned>(), 16);

    let mut signed = [0i128; 2];
    signed[0] = discriminant_value(&Signed::Positive);
    signed[1] = discriminant_value(&Signed::Negative);
    assert_eq!(signed, [0x1223_3445_5667_7889, -0x1223_3445_5667_7889]);

    let mut unsigned = [0u128; 1];
    unsigned[0] = discriminant_value(&Unsigned::Value);
    assert_eq!(unsigned, [0x9876_5432_10]);
}
