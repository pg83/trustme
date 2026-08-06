// Extracted from library/core/src/ptr/non_null.rs:894
#![allow(unused)]
fn main() {
    unsafe fn blah(ptr: std::ptr::NonNull<u32>, origin: std::ptr::NonNull<u32>, count: usize) -> bool { unsafe {
    ptr.offset_from_unsigned(origin) == count
    &&
    origin.add(count) == ptr
    &&
    ptr.sub(count) == origin
    } }
}
