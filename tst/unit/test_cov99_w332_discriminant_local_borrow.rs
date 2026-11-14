//@ compile-flags: -Z dump-mir
#![feature(core_intrinsics)]

enum Choice {
    First = 5,
    Second = 9,
}

const fn selected_tag() -> isize {
    let selected = Choice::Second;
    unsafe { core::intrinsics::discriminant_value(&selected) }
}

const TAG: isize = selected_tag();

fn main() {
    assert_eq!(TAG, 9);
}
