//@ compile-variant: -O
#![feature(core_intrinsics)]

const VALUES: (u8, i8, i128, u128) = unsafe {
    (
        core::intrinsics::unchecked_shl(1u8, 7),
        core::intrinsics::unchecked_shr(-65i8, 7),
        core::intrinsics::unchecked_shr(i128::MIN, 127),
        core::intrinsics::unchecked_shl(1u128, 127),
    )
};

fn main() {
    assert_eq!(VALUES, (128, -1, -1, 1u128 << 127));
    assert_eq!(1u8.wrapping_shl(8), 1);
    assert_eq!((-3i16).wrapping_shr(16), -3);
}
