#![feature(core_intrinsics, const_heap)]
const VALUE: u8 = unsafe {
    let pointer = std::intrinsics::const_allocate(0, 0);
    std::intrinsics::const_deallocate(pointer, 0, 1);
    41
};
fn main() { assert_eq!(VALUE, 41); }
