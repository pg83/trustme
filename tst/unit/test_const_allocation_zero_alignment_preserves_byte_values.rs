#![feature(core_intrinsics, const_heap)]
const VALUE: u8 = unsafe {
    let pointer = std::intrinsics::const_allocate(1, 0);
    pointer.write(37);
    let value = pointer.read();
    std::intrinsics::const_deallocate(pointer, 1, 1);
    value
};
fn main() { assert_eq!(VALUE, 37); }
