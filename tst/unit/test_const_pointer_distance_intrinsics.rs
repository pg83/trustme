#![feature(core_intrinsics)]
#![allow(internal_features)]

#[repr(C)]
struct Fields {
    first: u32,
    second: u32,
    third: u32,
}

static FIELDS: Fields = Fields { first: 10, second: 20, third: 30 };

const SIGNED_FORWARD: isize = unsafe {
    let values = [10u32, 20, 30, 40];
    core::intrinsics::ptr_offset_from(values.as_ptr().add(3), values.as_ptr())
};

const SIGNED_BACKWARD: isize = unsafe {
    let values = [10u32, 20, 30, 40];
    core::intrinsics::ptr_offset_from(values.as_ptr(), values.as_ptr().add(3))
};

const UNSIGNED: usize = unsafe {
    let values = [10u32, 20, 30, 40];
    core::intrinsics::ptr_offset_from_unsigned(values.as_ptr().add(2), values.as_ptr())
};

const STATIC_FIELDS: isize = unsafe {
    let first = &FIELDS.first as *const u32;
    let third = &FIELDS.third as *const u32;
    core::intrinsics::ptr_offset_from(third, first)
};

fn main() {
    assert_eq!(SIGNED_FORWARD, 3);
    assert_eq!(SIGNED_BACKWARD, -3);
    assert_eq!(UNSIGNED, 2);
    assert_eq!(STATIC_FIELDS, 2);
}
