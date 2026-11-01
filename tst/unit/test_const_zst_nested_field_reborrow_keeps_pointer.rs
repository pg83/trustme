#![feature(core_intrinsics)]
static EMPTY: u16 = 257;
const SAME: bool = unsafe {
    let pointer = (&EMPTY as *const u16).cast::<u8>().wrapping_sub(1).cast::<((), ((), ()))>();
    let value = &*pointer;
    std::intrinsics::ptr_guaranteed_cmp(&value.1.1 as *const (), pointer.cast::<()>()) != 0
};
fn main() { assert!(SAME); }
