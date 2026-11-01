//@ compile-variant: -O
#![feature(core_intrinsics)]
#[inline(never)]
fn count(first: bool, value: u32) -> u32 {
    if first { std::intrinsics::ctpop(value) } else { std::intrinsics::ctlz(value) }
}
#[inline(never)]
unsafe fn copy_selected(first: bool, from16: *const u16, to16: *mut u16, from32: *const u32, to32: *mut u32) {
    if first {
        std::intrinsics::copy_nonoverlapping(from16, to16, 1);
    } else {
        std::intrinsics::copy_nonoverlapping(from32, to32, 1);
    }
}
fn main() {
    assert_eq!(count(true, 0b1010), 2);
    assert_eq!(count(false, 0b1010), 28);
    let from16 = 13u16;
    let from32 = 0x12345678u32;
    let mut to16 = 0u16;
    let mut to32 = 0u32;
    unsafe { copy_selected(true, &from16, &mut to16, &from32, &mut to32); }
    assert_eq!((to16, to32), (13, 0));
    unsafe { copy_selected(false, &from16, &mut to16, &from32, &mut to32); }
    assert_eq!((to16, to32), (13, 0x12345678));
}
