#![feature(core_intrinsics, const_heap)]
const VALUE: u8 = unsafe {
    let pointer = std::intrinsics::const_allocate(1, 1);
    pointer.write(43);
    let value = pointer.read();
    std::intrinsics::const_deallocate(pointer, 1, 0);
    value
};
fn main() { assert_eq!(VALUE, 43); }
