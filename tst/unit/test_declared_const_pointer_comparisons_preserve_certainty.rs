#![feature(intrinsics)]
#![allow(internal_features)]
#[rustc_intrinsic]
const fn ptr_guaranteed_cmp<T>(left: *const T, right: *const T) -> u8;
const RESULTS: [u8; 6] = {
    let data = [17u8, 23];
    let first = data.as_ptr();
    [ptr_guaranteed_cmp(17usize as *const u8, 17usize as *const u8),
     ptr_guaranteed_cmp(17usize as *const u8, 19usize as *const u8),
     ptr_guaranteed_cmp(first, first),
     ptr_guaranteed_cmp(first, unsafe { first.add(1) }),
     ptr_guaranteed_cmp(first, std::ptr::null()),
     ptr_guaranteed_cmp(std::ptr::null(), first)]
};
fn main() {
    assert_eq!(&RESULTS[..2], &[1, 0]);
    assert!(RESULTS[2] == 1 || RESULTS[2] == 2);
    assert!(RESULTS[3] == 0 || RESULTS[3] == 2);
    assert!(RESULTS[4] == 0 || RESULTS[4] == 2);
    assert!(RESULTS[5] == 0 || RESULTS[5] == 2);
}
