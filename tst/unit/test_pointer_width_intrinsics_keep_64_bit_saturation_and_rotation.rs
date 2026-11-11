//@ compile-flags: -Zmir-opt-level=0
#![feature(core_intrinsics)]

#[inline(never)]
fn unsigned(value: usize, delta: usize, shift: u32) -> (usize, usize, usize, usize) {
    (
        core::intrinsics::saturating_add(value, delta),
        core::intrinsics::saturating_sub(value, delta),
        core::intrinsics::rotate_left(value, shift),
        core::intrinsics::rotate_right(value, shift),
    )
}

#[inline(never)]
fn signed(value: isize, delta: isize, shift: u32) -> (isize, isize, isize, isize) {
    (
        core::intrinsics::saturating_add(value, delta),
        core::intrinsics::saturating_sub(value, delta),
        core::intrinsics::rotate_left(value, shift),
        core::intrinsics::rotate_right(value, shift),
    )
}

fn main() {
    let high = std::hint::black_box(0x8000_0000_0000_0001usize);
    assert_eq!(unsigned(high, high, 65), (usize::MAX, 0, 3, 0xc000_0000_0000_0000));
    assert_eq!(unsigned(3, std::hint::black_box(4), 0), (7, 0, 3, 3));

    let maximum = std::hint::black_box(isize::MAX);
    let minimum = std::hint::black_box(isize::MIN);
    assert_eq!(signed(maximum, 1, 65), (maximum, maximum - 1, -2, -4_611_686_018_427_387_905));
    assert_eq!(signed(minimum, 1, 63), (minimum + 1, minimum, 4_611_686_018_427_387_904, 1));
    assert_eq!(signed(maximum, -1, 0), (maximum - 1, maximum, maximum, maximum));
    assert_eq!(signed(minimum, -1, 64), (minimum, minimum + 1, minimum, minimum));

    let narrow = std::hint::black_box(0x81u8);
    assert_eq!(
        (
            core::intrinsics::saturating_add(narrow, narrow),
            core::intrinsics::saturating_sub(narrow, 0xff),
            core::intrinsics::rotate_left(narrow, 9),
            core::intrinsics::rotate_right(narrow, 9),
        ),
        (0xff, 0, 3, 0xc0),
    );
}
