#![feature(core_intrinsics)]
#[repr(align(8192))]
struct Empty;
#[repr(align(16384))]
struct Backing(u8);
static VALUE: Backing = Backing(37);
const SAME: bool = unsafe {
    let pointer = (&VALUE as *const Backing).cast::<u8>().wrapping_sub(8192).cast::<Empty>();
    std::intrinsics::raw_eq(&*pointer, &*pointer)
};
fn main() { assert!(SAME); }
