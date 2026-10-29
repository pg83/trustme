//@ compile-flags: --target=$DIR/emulated_i128_target.toml -Coverflow-checks=off
//@ input: emulated_i128_target.toml
//@ output-contains: .cpp #define TRUSTME_TARGET_EMULATED_I128 1
#![feature(core_intrinsics)]
use std::hint::black_box;
#[inline(never)]
fn left_unsigned(v: u128, n: u32) -> u128 { core::intrinsics::rotate_left(v, n) }
#[inline(never)]
fn right_unsigned(v: u128, n: u32) -> u128 { core::intrinsics::rotate_right(v, n) }
#[inline(never)]
fn left_signed(v: i128, n: u32) -> i128 { core::intrinsics::rotate_left(v, n) }
#[inline(never)]
fn right_signed(v: i128, n: u32) -> i128 { core::intrinsics::rotate_right(v, n) }
fn main() {
    let value = black_box(0x0123456789abcdef_fedcba9876543210u128);
    let cases: &[(u32, u128, u128)] = &[
        (0, 0x0123456789abcdeffedcba9876543210, 0x0123456789abcdeffedcba9876543210),
        (1, 0x02468acf13579bdffdb97530eca86420, 0x0091a2b3c4d5e6f7ff6e5d4c3b2a1908),
        (63, 0xff6e5d4c3b2a19080091a2b3c4d5e6f7, 0xfdb97530eca8642002468acf13579bdf),
        (64, 0xfedcba98765432100123456789abcdef, 0xfedcba98765432100123456789abcdef),
        (65, 0xfdb97530eca8642002468acf13579bdf, 0xff6e5d4c3b2a19080091a2b3c4d5e6f7),
        (127, 0x0091a2b3c4d5e6f7ff6e5d4c3b2a1908, 0x02468acf13579bdffdb97530eca86420),
        (128, 0x0123456789abcdeffedcba9876543210, 0x0123456789abcdeffedcba9876543210),
        (129, 0x02468acf13579bdffdb97530eca86420, 0x0091a2b3c4d5e6f7ff6e5d4c3b2a1908),
        (191, 0xff6e5d4c3b2a19080091a2b3c4d5e6f7, 0xfdb97530eca8642002468acf13579bdf),
        (192, 0xfedcba98765432100123456789abcdef, 0xfedcba98765432100123456789abcdef),
        (193, 0xfdb97530eca8642002468acf13579bdf, 0xff6e5d4c3b2a19080091a2b3c4d5e6f7),
        (255, 0x0091a2b3c4d5e6f7ff6e5d4c3b2a1908, 0x02468acf13579bdffdb97530eca86420),
        (4294967295, 0x0091a2b3c4d5e6f7ff6e5d4c3b2a1908, 0x02468acf13579bdffdb97530eca86420),
    ];
    for &(shift, left, right) in cases {
        let n = black_box(shift);
        assert_eq!(left_unsigned(value, n), left);
        assert_eq!(right_unsigned(value, n), right);
        assert_eq!(left_signed(value as i128, n) as u128, left);
        assert_eq!(right_signed(value as i128, n) as u128, right);
        assert_eq!(left_signed((!value) as i128, n) as u128, !left);
        assert_eq!(right_signed((!value) as i128, n) as u128, !right);
        assert_eq!(left_unsigned(black_box(0), n), 0);
        assert_eq!(right_unsigned(black_box(u128::MAX), n), u128::MAX);
        assert_eq!(left_unsigned(right_unsigned(value, n), n), value);
        assert_eq!(right_signed(left_signed(value as i128, n), n) as u128, value);
    }
}
