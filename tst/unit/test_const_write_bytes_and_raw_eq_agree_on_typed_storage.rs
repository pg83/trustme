#![feature(core_intrinsics)]
#![allow(internal_features)]
const fn written() -> ([u32; 3], bool) {
    let mut values = [7u32, 11, 13];
    unsafe { core::ptr::write_bytes(values.as_mut_ptr().add(1), 0x5a, 2); }
    let expected = [7u32, 0x5a5a5a5a, 0x5a5a5a5a];
    let equal = unsafe { core::intrinsics::raw_eq(&values, &expected) };
    (values, equal)
}
const VALUE: ([u32; 3], bool) = written();
fn main() { assert_eq!(VALUE, ([7, 0x5a5a5a5a, 0x5a5a5a5a], true)); }
