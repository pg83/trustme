//@ compile-variant: -O
#![feature(core_intrinsics)]
#[inline(never)]
unsafe fn fill(wide: bool, target: *mut u32) {
    if wide {
        return std::intrinsics::write_bytes::<u32>(target, 0x7b, 2);
    } else {
        return std::intrinsics::write_bytes::<u8>(target.cast(), 0x31, 2);
    }
}
#[inline(never)]
unsafe fn copy(wide: bool, source: *const u32, target: *mut u32) {
    if wide {
        return std::intrinsics::copy_nonoverlapping::<u32>(source, target, 2);
    } else {
        return std::intrinsics::copy_nonoverlapping::<u8>(source.cast(), target.cast(), 2);
    }
}
fn main() {
    let mut output = [0u32; 2];
    unsafe { fill(false, output.as_mut_ptr()); }
    assert_eq!(output, [u32::from_ne_bytes([0x31, 0x31, 0, 0]), 0]);
    unsafe { fill(true, output.as_mut_ptr()); }
    assert_eq!(output, [0x7b7b7b7b; 2]);
    let source = [0x17171717u32, 0x29292929];
    output = [0; 2];
    unsafe { copy(false, source.as_ptr(), output.as_mut_ptr()); }
    assert_eq!(output, [u32::from_ne_bytes([0x17, 0x17, 0, 0]), 0]);
    unsafe { copy(true, source.as_ptr(), output.as_mut_ptr()); }
    assert_eq!(output, source);
}
