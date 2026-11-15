//@ compile-flags: --target x86_64-unknown-linux-gnu -Zmir-opt-level=0
#![feature(core_intrinsics)]

#[inline(never)]
fn saturated_pair(value: u128, delta: u128) -> (u128, u128) {
    (
        core::intrinsics::saturating_add(value, delta),
        core::intrinsics::saturating_sub(value, delta),
    )
}

fn main() {
    let maximum = std::hint::black_box(u128::MAX);
    assert_eq!(saturated_pair(maximum, 1), (u128::MAX, u128::MAX - 1));
    assert_eq!(saturated_pair(std::hint::black_box(0), 1), (1, 0));

    let wide = std::hint::black_box(1u128 << 100);
    assert_eq!(saturated_pair(wide, 7), ((1u128 << 100) + 7, (1u128 << 100) - 7));
}
