#![feature(core_intrinsics)]
static BYTE: u16 = 257;
const VALUE: (usize, u8) = unsafe {
    let pointer = (&BYTE as *const u16).cast::<u8>().wrapping_sub(1);
    let reference = std::str::from_utf8_unchecked(&*std::ptr::slice_from_raw_parts(pointer, 0));
    (reference.len(), std::intrinsics::ptr_guaranteed_cmp(reference.as_ptr(), pointer))
};
fn main() { assert_eq!(VALUE.0, 0); assert!(VALUE.1 == 1 || VALUE.1 == 2); }
