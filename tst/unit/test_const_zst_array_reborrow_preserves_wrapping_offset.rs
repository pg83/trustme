#![feature(core_intrinsics)]
#[repr(align(8192))]
struct Backing(u8);
static EMPTY: Backing = Backing(37);
const SAME: bool = unsafe {
    let pointer = (&EMPTY as *const Backing).cast::<u8>().wrapping_sub(4096).cast::<[(); 3]>();
    let value = &*pointer;
    std::intrinsics::ptr_guaranteed_cmp(&value[2] as *const (), pointer.cast::<()>()) != 0
};
fn main() { assert!(SAME); }
