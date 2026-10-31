#![feature(core_intrinsics)]
#![allow(internal_features)]
struct Pair(u8, u16);
#[repr(i16)]
enum Tag { First = -7, Second = 23 }
const ORDINARY: [u8; 3] = [
    std::intrinsics::discriminant_value(&17u32),
    std::intrinsics::discriminant_value(&Pair(19, 23)),
    std::intrinsics::discriminant_value(&[31u8, 37]),
];
const ENUMS: [i16; 2] = [std::intrinsics::discriminant_value(&Tag::First), std::intrinsics::discriminant_value(&Tag::Second)];
fn main() { assert_eq!(ORDINARY, [0; 3]); assert_eq!(ENUMS, [-7, 23]); }
