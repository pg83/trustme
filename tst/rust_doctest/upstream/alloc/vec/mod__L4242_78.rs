// Extracted from library/alloc/src/vec/mod.rs:4242
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut v = String::from("hello world").into_bytes();
    v.sort();
    v.truncate(2);
    let [a, b]: [_; 2] = v.try_into().unwrap();
    assert_eq!(a, b' ');
    assert_eq!(b, b'd');
}
