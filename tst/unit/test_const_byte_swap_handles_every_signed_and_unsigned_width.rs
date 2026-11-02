#![feature(core_intrinsics)]
const UNSIGNED: (u8, u16, u32, u64, u128, usize) = (
    core::intrinsics::bswap(0x53u8),
    core::intrinsics::bswap(0x53u16),
    core::intrinsics::bswap(0x53u32),
    core::intrinsics::bswap(0x53u64),
    core::intrinsics::bswap(0x53u128),
    core::intrinsics::bswap(0x53usize),
);
const SIGNED: (i8, i16, i32, i64, i128, isize) = (
    core::intrinsics::bswap(-2i8),
    core::intrinsics::bswap(-2i16),
    core::intrinsics::bswap(-2i32),
    core::intrinsics::bswap(-2i64),
    core::intrinsics::bswap(-2i128),
    core::intrinsics::bswap(-2isize),
);

fn main() {
    assert_eq!(UNSIGNED, (0x53, 0x5300, 0x53000000, 0x5300000000000000, 0x53u128 << 120, 0x53usize << (usize::BITS - 8)));
    assert_eq!(SIGNED, (-2, -257, -16777217, -(1i64 << 56) - 1, -(1i128 << 120) - 1, -(1isize << (isize::BITS - 8)) - 1));
}
