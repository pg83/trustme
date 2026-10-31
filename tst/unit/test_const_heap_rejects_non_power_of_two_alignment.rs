//@ compile-fail: requires a valid power-of-two alignment
#![feature(core_intrinsics, const_heap)]
const VALUE: u8 = unsafe {
    let pointer = std::intrinsics::const_allocate(1, 3);
    pointer.write(37);
    let value = pointer.read();
    std::intrinsics::const_deallocate(pointer, 1, 3);
    value
};
fn main() { assert_eq!(VALUE, 37); }
