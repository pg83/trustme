// Extracted from library/core/src/ptr/const_ptr.rs:708
#![allow(unused)]
fn main() {
    unsafe fn blah(ptr: *const i32, origin: *const i32, count: usize) -> bool { unsafe {
    ptr.offset_from_unsigned(origin) == count
    &&
    origin.add(count) == ptr
    &&
    ptr.sub(count) == origin
    } }
}
