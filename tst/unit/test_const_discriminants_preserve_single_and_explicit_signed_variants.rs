//@ compile-variant: -O
#![feature(core_intrinsics)]
#[repr(i16)]
enum Signed { First = -17, Second = 43 }
#[derive(Copy, Clone)]
enum Solo { Only = 29 }
const fn signed(value: &Signed) -> i16 { std::intrinsics::discriminant_value(value) }
const fn solo(value: &Solo) -> isize { std::intrinsics::discriminant_value(value) }
const VALUES: (i16, i16, isize, u16, i32) = (
    signed(&Signed::First), signed(&Signed::Second), solo(&Solo::Only), Solo::Only as u16, Solo::Only as i32,
);
fn main() { assert_eq!(VALUES, (-17, 43, 29, 29, 29)); }
