//@ compile-variant: --target=$DIR/emulated_i128_target.toml -Zmir-opt-level=0
//@ input: emulated_i128_target.toml
#![feature(f128)]
#[repr(i16)]
enum Tag { Below = -631, Above = 641 }
enum Single { Only = -643 }
fn main() {
    let magnitude = (1i128 << 100) + (1i128 << 60) + 17;
    let signed = std::hint::black_box(-magnitude);
    let unsigned = std::hint::black_box(magnitude as u128);
    assert_eq!((signed as f128) as i128, signed);
    assert_eq!((unsigned as f128) as u128, unsigned);
    assert_eq!((std::hint::black_box(i128::MIN) as f128) as i128, i128::MIN);
    assert_eq!((std::hint::black_box(i128::MAX) as f128) as i128, i128::MAX);
    assert_eq!(signed as f32, -1267650600228229401496703205376.0);
    assert_eq!(unsigned as f32, 1267650600228229401496703205376.0);
    let exact_power = std::hint::black_box(1u128 << 100);
    assert_eq!(exact_power as f64, 1267650600228229401496703205376.0);
    assert_eq!((-(exact_power as i128)) as f64, -1267650600228229401496703205376.0);
    let low = std::hint::black_box(647u128);
    let negative = std::hint::black_box(-653i128);
    macro_rules! narrow {
        ($integer:ty) => {{ assert_eq!(low as $integer, 647u128 as $integer); assert_eq!(negative as $integer, -653i128 as $integer); }};
    }
    narrow!(i8); narrow!(u8); narrow!(i16); narrow!(u16); narrow!(i32); narrow!(u32);
    narrow!(i64); narrow!(u64); narrow!(isize); narrow!(usize);
    assert_eq!((signed as u128) as i128, signed);
    assert_eq!(std::hint::black_box(-17i8) as i128, -17);
    assert_eq!(std::hint::black_box(659u64) as u128, 659);
    assert_eq!(std::hint::black_box(Tag::Below) as i128, -631);
    assert_eq!(std::hint::black_box(Tag::Above) as u128, 641);
    assert_eq!(std::hint::black_box(Single::Only) as i128, -643);
}
