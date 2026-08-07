// Extracted from library/alloc/src/string.rs:1375
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::from("foo");

    s.reserve(100);
    assert!(s.capacity() >= 100);

    s.shrink_to(10);
    assert!(s.capacity() >= 10);
    s.shrink_to(0);
    assert!(s.capacity() >= 3);
}
