#![feature(core_intrinsics)]
#![allow(internal_features)]
#[no_mangle]
pub unsafe fn narrow(pointer: *const [u8]) -> *const u8 {
    core::intrinsics::transmute_unchecked(pointer)
}
#[no_mangle]
pub unsafe fn widen(pointer: *const u8) -> *const [u8] {
    core::intrinsics::transmute_unchecked(pointer)
}
fn main() {
    let one: f32 = unsafe { core::intrinsics::transmute_unchecked(0x3f800000u32) };
    assert_eq!(one, 1.0);
    let values = [9u8, 8, 7];
    let pointer = &values[..] as *const [u8];
    let parts: (*const u8, usize) = unsafe { core::intrinsics::transmute_unchecked(pointer) };
    assert_eq!(parts.0, values.as_ptr());
    assert_eq!(parts.1, 3);
}
