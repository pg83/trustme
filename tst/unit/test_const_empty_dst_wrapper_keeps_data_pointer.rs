#![feature(core_intrinsics)]
#[repr(C)]
struct Wrapper<T: ?Sized> { marker: (), tail: T }
static BYTE: u16 = 257;
const VALUE: (usize, u8) = unsafe {
    let pointer = (&BYTE as *const u16).cast::<u8>().wrapping_sub(1);
    let reference = &*(std::ptr::slice_from_raw_parts(pointer, 0) as *const Wrapper<[u8]>);
    (std::mem::size_of_val(reference), std::intrinsics::ptr_guaranteed_cmp(reference.tail.as_ptr(), pointer))
};
fn main() { assert_eq!(VALUE.0, 0); assert!(VALUE.1 == 1 || VALUE.1 == 2); }
