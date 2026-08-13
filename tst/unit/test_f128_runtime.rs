#![feature(f128)]

fn main() {
    assert_eq!(core::mem::size_of::<f128>(), 16);
    assert_eq!(core::mem::align_of::<f128>(), 16);

    assert!(f128::NAN.is_nan());
    assert!(!7.0f128.is_nan());
    assert!(f128::INFINITY.is_infinite());
    assert!((-7.0f128).is_sign_negative());

    assert_eq!(1.5f128 + 2.25, 3.75);
    assert_eq!(7.0f128 - 2.0, 5.0);
    assert_eq!(3.0f128 * 2.0, 6.0);
    assert_eq!(7.0f128 / 2.0, 3.5);
    assert_eq!(-3.5f128, 0.0 - 3.5);

    assert_eq!((-3.5f128).abs(), 3.5);
    assert_eq!(3.5f128.copysign(-1.0), -3.5);

    let unsigned = unsafe { 4.6f128.to_int_unchecked::<u16>() };
    let signed = unsafe { (-128.9f128).to_int_unchecked::<i8>() };
    assert_eq!(unsigned, 4);
    assert_eq!(signed, i8::MIN);

    assert_eq!(f128::from_bits(0x40029000000000000000000000000000), 12.5);
    assert!(1.0f128.next_down() < 1.0);
}
