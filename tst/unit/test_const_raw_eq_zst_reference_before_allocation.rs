#![feature(core_intrinsics)]
static EMPTY: u16 = 257;
const SAME: bool = unsafe {
    let pointer = (&EMPTY as *const u16).cast::<u8>().wrapping_sub(1).cast::<()>();
    std::intrinsics::raw_eq(&*pointer, &*pointer)
};
fn main() { assert!(SAME); }
