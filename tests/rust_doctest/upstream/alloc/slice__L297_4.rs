// Extracted from library/alloc/src/slice.rs:297
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut v = [4i32, -5, 1, -3, 2, 10];
    
    // Strings are sorted by lexicographical order.
    v.sort_by_cached_key(|k| k.to_string());
    assert_eq!(v, [-3, -5, 1, 10, 2, 4]);
}
