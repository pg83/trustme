#![feature(intrinsics)]
#![allow(internal_features)]

#[rustc_intrinsic]
fn ptr_guaranteed_cmp<T>(left: *const T, right: *const T) -> u8;

#[inline(never)]
fn compare(left: *const u8, right: *const u8) -> u8 {
    ptr_guaranteed_cmp(std::hint::black_box(left), std::hint::black_box(right))
}

fn main() {
    let values = [31u8, 47];
    let first = values.as_ptr();
    let second = unsafe { first.add(1) };
    assert_eq!(compare(first, first), 1);
    assert_eq!(compare(first, second), 0);
    assert_eq!(compare(std::ptr::null(), std::ptr::null()), 1);
    assert_eq!(compare(first, std::ptr::null()), 0);
}
