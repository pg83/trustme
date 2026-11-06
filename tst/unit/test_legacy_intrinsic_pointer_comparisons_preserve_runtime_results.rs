#![feature(intrinsics)]
#![allow(internal_features)]

extern "rust-intrinsic" {
    fn ptr_guaranteed_eq<T>(left: *const T, right: *const T) -> bool;
    fn ptr_guaranteed_ne<T>(left: *const T, right: *const T) -> bool;
}

#[inline(never)]
fn compare(left: *const u16, right: *const u16) -> (bool, bool) {
    unsafe {
        let left = std::ptr::read_volatile(&left);
        let right = std::ptr::read_volatile(&right);
        (ptr_guaranteed_eq(left, right), ptr_guaranteed_ne(left, right))
    }
}

fn main() {
    let values = [11u16, 29];
    let first = values.as_ptr();
    let second = unsafe { first.add(1) };
    assert_eq!(compare(first, first), (true, false));
    assert_eq!(compare(first, second), (false, true));
    assert_eq!(compare(std::ptr::null(), std::ptr::null()), (true, false));
    assert_eq!(compare(second, std::ptr::null()), (false, true));
}
