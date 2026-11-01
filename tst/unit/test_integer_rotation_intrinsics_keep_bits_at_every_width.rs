//@ compile-variant: --target=$DIR/emulated_i128_target.toml
#![feature(core_intrinsics, cfg_version)]

fn left_bits(value: u128, width: u32, shift: u32) -> u128 {
    let mut result = 0;
    for bit in 0..width {
        if (value >> bit) & 1 != 0 {
            result |= 1u128 << ((bit + shift % width) % width);
        }
    }
    result
}

macro_rules! check {
    ($integer:ty, $unsigned:ty, $width:expr) => {{
        let width = $width;
        let mask = u128::MAX >> (128 - width);
        for bits in [0, 1, 5, mask, (1u128 << (width - 1)) | 5] {
            let value = std::hint::black_box(bits as $integer);
            for amount in [0, 1, width - 1, width, width + 1, width * 2 + 5, u32::MAX] {
                let amount = std::hint::black_box(amount);
                #[cfg(not(version("1.96")))]
                let (left, right) = (
                    core::intrinsics::rotate_left(value, amount),
                    core::intrinsics::rotate_right(value, amount),
                );
                #[cfg(version("1.96"))]
                let (left, right) = (
                    core::intrinsics::rotate_left(value as $unsigned, amount) as $integer,
                    core::intrinsics::rotate_right(value as $unsigned, amount) as $integer,
                );
                assert_eq!((left as u128) & mask, left_bits(bits, width, amount));
                assert_eq!((right as u128) & mask, left_bits(bits, width, (width - amount % width) % width));
            }
        }
    }};
}

fn main() {
    check!(u8, u8, 8u32);
    check!(i8, u8, 8u32);
    check!(u16, u16, 16u32);
    check!(i16, u16, 16u32);
    check!(u32, u32, 32u32);
    check!(i32, u32, 32u32);
    check!(u64, u64, 64u32);
    check!(i64, u64, 64u32);
    check!(u128, u128, 128u32);
    check!(i128, u128, 128u32);
}
