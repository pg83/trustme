//@ compile-variant: --target=$DIR/emulated_i128_target.toml
#![feature(core_intrinsics, cfg_version)]

fn expected(value: u128, width: u32) -> (u32, u32, u32) {
    let mut ones = 0;
    let mut leading = width;
    let mut trailing = width;
    for bit in 0..width {
        if (value >> bit) & 1 != 0 {
            ones += 1;
            leading = width - bit - 1;
            if trailing == width { trailing = bit; }
        }
    }
    (ones, leading, trailing)
}

macro_rules! check {
    ($integer:ty, $unsigned:ty, $width:expr) => {{
        let width = $width;
        let mask = u128::MAX >> (128 - width);
        for bits in [0, 1, 5, mask, 1u128 << (width - 1), 1u128 << (width / 2)] {
            #[cfg(not(version("1.96")))]
            let value = std::hint::black_box(bits as $integer);
            #[cfg(version("1.96"))]
            let value = std::hint::black_box(bits as $integer as $unsigned);
            let (ones, leading, trailing) = expected(bits, width);
            assert_eq!(core::intrinsics::ctpop(value), ones);
            assert_eq!(core::intrinsics::ctlz(value), leading);
            assert_eq!(core::intrinsics::cttz(value), trailing);
            if bits != 0 {
                assert_eq!(unsafe { core::intrinsics::ctlz_nonzero(value) }, leading);
                assert_eq!(unsafe { core::intrinsics::cttz_nonzero(value) }, trailing);
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
