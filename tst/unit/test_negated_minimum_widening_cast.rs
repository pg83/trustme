// A negated minimum literal is folded into the literal, which must then carry
// the value sign-extended: a cast that widens it reads the sign, not a positive
// magnitude.
#![allow(overflowing_literals)]

fn main() {
    assert_eq!(-2147483648i32 as u64, 0xffffffff80000000);
    assert_eq!(-2147483648i32 as i64, -2147483648);
    assert_eq!(-128i8 as u64, 0xffffffffffffff80);
    assert_eq!(i64::MIN as u64, 0x8000000000000000);
    assert_eq!(-0x8000_0000_0000_0000_0000_0000_0000_0000i128, i128::MIN);
}
