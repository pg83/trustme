#![feature(lang_items, no_core, start)]
#![no_core]

#[lang = "sized"]
pub trait Sized {}

fn main() -> i32 {
    let arithmetic_ops = (9i32 - 3) + (4 * 2) + (8 / 2) + (8 % 3);

    let mut arithmetic = 9i32 + 3;
    arithmetic -= 2;
    arithmetic *= 3;
    arithmetic /= 5;
    arithmetic %= 4;
    arithmetic += 6;

    let binary_bits = ((0b1010u32 & 0b1100) | 1) ^ 4;
    let shifted = (3u32 << 2u8) + (16u32 >> 1u16);

    let mut bits = 0b1010u32;
    bits &= 0b1100;
    bits |= 1;
    bits ^= 4;
    bits <<= 2u8;
    bits >>= 1u16;

    let negative = -arithmetic;
    let inverted = !bits;
    let boolean = !false;

    if negative == -8
        && arithmetic_ops == 20
        && arithmetic != 3
        && arithmetic < 9
        && arithmetic <= 8
        && bits > 0
        && bits >= 2
        && binary_bits == 13
        && shifted == 20
        && inverted == !26u32
        && boolean
    {
        0
    } else {
        1
    }
}

#[start]
fn start(_argc: isize, _argv: *const *const u8) -> isize {
    main() as isize
}
