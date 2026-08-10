// Extracted from library/core/src/ptr/mut_ptr.rs:882
#![allow(unused)]
fn main() {
    unsafe fn blah(ptr: *mut i32, origin: *mut i32, count: usize) -> bool { unsafe {
    ptr.offset_from_unsigned(origin) == count
    &&
    origin.add(count) == ptr
    &&
    ptr.sub(count) == origin
    } }
}
