// The minimum value of a signed type is written as a negated literal, and its
// magnitude has no positive counterpart. Negating it at run time traps, so the
// negation belongs in the literal -- the two's-complement pattern of the
// minimum is exactly that magnitude.
//
// Same shape as the upstream test issues/issue-38987.rs.
fn main() {
    let a = -0x8000_0000_0000_0000_0000_0000_0000_0000i128;
    assert_eq!(a, i128::MIN);

    let b = -9223372036854775808i64;
    assert_eq!(b, i64::MIN);

    let c = -2147483648i32;
    assert_eq!(c, i32::MIN);

    let d = -32768i16;
    assert_eq!(d, i16::MIN);

    let e = -128i8;
    assert_eq!(e, i8::MIN);

    let f = -9223372036854775808isize;
    assert_eq!(f, isize::MIN);

    // Ordinary negations are unchanged.
    assert_eq!(-1i32, -1);
    assert_eq!(-(1i32 + 1), -2);
    let g = 5i32;
    assert_eq!(-g, -5);
    assert_eq!(-i32::MAX, -2147483647);
}
