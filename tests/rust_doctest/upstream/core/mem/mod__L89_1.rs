// Extracted from library/core/src/mem/mod.rs:89
#![allow(unused)]
fn main() {
    use std::mem;
    
    let mut v = vec![65, 122];
    // Build a `String` using the contents of `v`
    let s = unsafe { String::from_raw_parts(v.as_mut_ptr(), v.len(), v.capacity()) };
    // leak `v` because its memory is now managed by `s`
    mem::forget(v);  // ERROR - v is invalid and must not be passed to a function
    assert_eq!(s, "Az");
    // `s` is implicitly dropped and its memory deallocated.
}
