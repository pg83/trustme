//@ compile-fail: intrinsic `type_id` must be called in a const block
#![feature(core_intrinsics)]

#[inline(never)]
fn exercise() -> bool {
    let first = core::intrinsics::type_id::<u16>();
    let second = core::intrinsics::type_id::<u32>();
    first == core::intrinsics::type_id::<u16>() && first != second
}

fn main() { assert!(exercise()); }
