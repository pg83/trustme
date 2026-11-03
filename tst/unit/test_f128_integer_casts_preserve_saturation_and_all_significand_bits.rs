//@ compile-variant: --target=$DIR/emulated_i128_target.toml -Zmir-opt-level=0
//@ input: emulated_i128_target.toml
#![feature(f128)]
fn main() {
    let nan = std::hint::black_box(f128::NAN);
    let positive = std::hint::black_box(f128::INFINITY);
    let negative = std::hint::black_box(f128::NEG_INFINITY);
    let fraction = std::hint::black_box(1.75f128);
    let below_zero = std::hint::black_box(-0.75f128);
    macro_rules! check {
        ($integer:ty) => {{
            assert_eq!(nan as $integer, 0);
            assert_eq!(positive as $integer, <$integer>::MAX);
            assert_eq!(negative as $integer, <$integer>::MIN);
            assert_eq!(fraction as $integer, 1);
            assert_eq!(below_zero as $integer, 0);
        }};
    }
    check!(i8); check!(u8); check!(i16); check!(u16); check!(i32); check!(u32);
    check!(i64); check!(u64); check!(isize); check!(usize); check!(i128); check!(u128);
    let exact = (1u128 << 100) + (1u128 << 60) + 17;
    let value = std::hint::black_box(exact as f128);
    assert_eq!(value as u128, exact);
    assert_eq!((-value) as i128, -(exact as i128));
    let below_signed_limit = (1u128 << 127) - (1u128 << 14);
    assert_eq!(std::hint::black_box(below_signed_limit as f128) as i128, below_signed_limit as i128);
    let below_unsigned_limit = u128::MAX - ((1u128 << 15) - 1);
    assert_eq!(std::hint::black_box(below_unsigned_limit as f128) as u128, below_unsigned_limit);
}
