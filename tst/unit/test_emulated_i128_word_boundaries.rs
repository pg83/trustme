//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Coverflow-checks=off
//@ input: emulated_i128_target.toml
//@ output-contains: .cpp #define TRUSTME_TARGET_EMULATED_I128 1

use std::hint::black_box;

fn main() {
    let word = black_box(1u128 << 64);
    let low = black_box(0xffff_ffff_ffff_ffffu128);
    assert!(low + 1 == word);
    assert!(word - 1 == low);
    assert!((word + 9) * 3 == (3u128 << 64) + 27);
    assert!(((3u128 << 64) + 27) / (word + 9) == 3);
    assert!(((3u128 << 64) + 29) % (word + 9) == 2);
    assert!((low | word) == ((1u128 << 65) - 1));
    assert!((low & word) == 0 && (low ^ word) == low + word);
    assert!((low << black_box(64u128)) == (u128::MAX - low));
    assert!((u128::MAX >> black_box(64u128)) == low);
    assert!(word > low && word >= low && low < word && low <= word);
    assert!(word != low && black_box(word) == word);
    let negative = black_box(-((1i128 << 64) + 9));
    assert!(negative + 9 == -(1i128 << 64));
    assert!(negative - 9 == -((1i128 << 64) + 18));
    assert!(negative * 3 == -((3i128 << 64) + 27));
    assert!(negative / 3 == -6148914691236517208);
    assert!(negative % 3 == -1);
    assert!(negative >> 64 == -2 && negative << 1 == -((1i128 << 65) + 18));
    assert!(negative < -1 && negative <= -1 && -negative > 1 && -negative >= 1);
    assert!((negative | 0) == negative && (negative & -1) == negative);
    assert!((negative ^ -1) == -negative - 1);
    assert!(word.checked_add(low) == Some((1u128 << 65) - 1));
    assert!(word.checked_sub(word + 1) == None);
    assert!((word + 9).checked_mul(3) == Some((3u128 << 64) + 27));
    let (wrapped, overflow) = black_box(i128::MAX).overflowing_add(1);
    assert!(wrapped == i128::MIN && overflow);
}
