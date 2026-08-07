// Extracted from library/core/src/mem/mod.rs:115
#![allow(unused)]
fn main() {
    use std::mem::ManuallyDrop;

    let v = vec![65, 122];
    // Before we disassemble `v` into its raw parts, make sure it
    // does not get dropped!
    let mut v = ManuallyDrop::new(v);
    // Now disassemble `v`. These operations cannot panic, so there cannot be a leak.
    let (ptr, len, cap) = (v.as_mut_ptr(), v.len(), v.capacity());
    // Finally, build a `String`.
    let s = unsafe { String::from_raw_parts(ptr, len, cap) };
    assert_eq!(s, "Az");
    // `s` is implicitly dropped and its memory deallocated.
}
