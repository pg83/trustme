//@ compile-variant: --target=$DIR/emulated_i128_target.toml
#![feature(core_intrinsics)]

fn main() {
    let denominator = std::hint::black_box(1u128 << 64);
    let value = std::hint::black_box((1u128 << 100) + 37);
    assert_eq!(unsafe { core::intrinsics::unchecked_rem(value, denominator) }, 37);
    assert_eq!(unsafe { core::intrinsics::unchecked_rem(u128::MAX, denominator) }, denominator - 1);
    let denominator = std::hint::black_box(1i128 << 64);
    let value = std::hint::black_box(-((1i128 << 100) + 37));
    assert_eq!(unsafe { core::intrinsics::unchecked_rem(value, denominator) }, -37);
    assert_eq!(unsafe { core::intrinsics::unchecked_rem(value, -denominator) }, -37);
    assert_eq!(unsafe { core::intrinsics::unchecked_rem(-value, -denominator) }, 37);
    assert_eq!(unsafe { core::intrinsics::unchecked_rem(i128::MIN, 3) }, -2);
}
