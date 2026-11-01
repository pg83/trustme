//@ compile-flags: -Zmir-opt-level=0
#![feature(core_intrinsics)]

enum Empty {}
enum Single { Value(u16) }
enum Later { First(Empty), Second(Empty), Value(u16) }

fn empty_discriminant(value: &Empty) -> isize {
    core::intrinsics::discriminant_value(value)
}

fn single_discriminant(value: &Single) -> isize {
    core::intrinsics::discriminant_value(value)
}

fn later_discriminant(value: &Later) -> isize {
    core::intrinsics::discriminant_value(value)
}

fn main() {
    let byte = std::hint::black_box(0xA5u8);
    let signed = std::hint::black_box(-37i8);
    assert_eq!(core::intrinsics::bswap(byte), 0xA5);
    assert_eq!(core::intrinsics::bswap(signed), -37);
    let value = Single::Value(31);
    assert_eq!(single_discriminant(std::hint::black_box(&value)), 0);
    let Single::Value(payload) = value;
    assert_eq!(payload, 31);
    let later = Later::Value(37);
    assert_eq!(later_discriminant(std::hint::black_box(&later)), 2);
    match later {
        Later::Value(payload) => assert_eq!(payload, 37),
        Later::First(empty) | Later::Second(empty) => match empty {},
    }
    let address = std::hint::black_box(empty_discriminant as fn(&Empty) -> isize);
    assert_ne!(address as usize, 0);
}
