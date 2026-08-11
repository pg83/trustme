fn main() {
    assert_eq!(0u8.leading_zeros(), 8);
    assert_eq!(1u8.leading_zeros(), 7);
    assert_eq!(u8::MAX.leading_zeros(), 0);
    assert_eq!((-1i8).leading_zeros(), 0);

    assert_eq!(0u16.leading_zeros(), 16);
    assert_eq!(1u16.leading_zeros(), 15);
    assert_eq!(u16::MAX.leading_zeros(), 0);
    assert_eq!((-1i16).leading_zeros(), 0);
}
