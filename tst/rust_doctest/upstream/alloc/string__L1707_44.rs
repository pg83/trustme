// Extracted from library/alloc/src/string.rs:1707
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::with_capacity(3);

    s.insert(0, 'f');
    s.insert(1, 'o');
    s.insert(2, 'o');

    assert_eq!("foo", s);
}
