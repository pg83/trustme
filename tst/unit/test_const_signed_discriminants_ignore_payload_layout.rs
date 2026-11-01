#![feature(core_intrinsics)]
#![allow(internal_features)]
#[repr(i16)]
enum Kind { Small(u8) = -19, Wide([u32; 2]) = 41 }
const SMALL: i16 = core::intrinsics::discriminant_value(&Kind::Small(7)) as i16;
const WIDE: i16 = core::intrinsics::discriminant_value(&Kind::Wide([11, 13])) as i16;
fn main() { assert_eq!((SMALL, WIDE), (-19, 41)); }
