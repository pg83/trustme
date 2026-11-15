//@ crate-type: lib
//@ compile-only
//@ compile-flags: -Cpanic=abort -Cemit-cpp-only -Zmir-opt-level=0

pub enum Empty {}

impl Drop for Empty {
    fn drop(&mut self) {}
}

pub unsafe fn drop_empty(pointer: *mut Empty) {
    std::ptr::drop_in_place(pointer);
}
