// Extracted from library/alloc/src/vec/mod.rs:2137
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut v = vec!['a', 'b', 'c'];
    assert_eq!(v.remove(1), 'b');
    assert_eq!(v, ['a', 'c']);
}
