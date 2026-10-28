//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Coverflow-checks=off
//@ input: emulated_i128_target.toml
//@ output-contains: .cpp #define TRUSTME_TARGET_EMULATED_I128 1

use std::hint::black_box;

enum Single { Only = -3 }
enum Choice { Negative = -4, Positive = 9 }

fn main() {
    let high = black_box(1u128 << 100);
    let small = black_box(7u128);
    assert!(high + small - high == 7);
    assert!(high * small / small == high);
    assert!((high + small) % high == small);
    assert!((high | small) & high == high);
    assert!((high ^ small) ^ small == high);
    assert!(!high == u128::MAX - high);
    assert!(high >> 96 == 16);
    assert!(small << 65 == 7 * (1u128 << 65));
    assert!(small << black_box(65u128) == 7 * (1u128 << 65));
    assert!(high > small && high >= high && small < high && small <= small);
    assert!(high != small && high == high);
    let negative = black_box(-17i128);
    assert!(negative / 4 == -4);
    assert!(negative % 4 == -1);
    assert!(-negative == 17);
    assert!(negative >> 2 == -5);
    assert!(negative < 0 && negative <= -17 && negative != 17);
    assert!((negative as u128) as i128 == negative);
    assert!(high as u64 == 0 && small as u8 == 7);
    assert!(negative as i16 == -17 && negative as i32 == -17);
    assert!(negative as i64 == -17 && negative as isize == -17);
    assert!(black_box(-123i32) as i128 == -123);
    assert!(black_box(456u32) as u128 == 456);
    assert!(black_box(1.5f64) as u128 == 1);
    assert!(black_box(-2.75f32) as i128 == -2);
    assert!(high as f64 == 1267650600228229401496703205376.0);
    assert!(negative as f32 == -17.0);
    assert!(negative as f64 == -17.0);
    assert!(high as f32 == 1267650600228229401496703205376.0);
    assert!(black_box(Single::Only) as i128 == -3);
    assert!(black_box(Choice::Negative) as i128 == -4);
    assert!(black_box(Choice::Positive) as u128 == 9);
}
