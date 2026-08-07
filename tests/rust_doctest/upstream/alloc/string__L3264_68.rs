// Extracted from library/alloc/src/string.rs:3264
#![allow(unused)]
extern crate alloc;
fn main() {
    let s1 = b"hello world".to_vec();
    let v1 = String::try_from(s1).unwrap();
    assert_eq!(v1, "hello world");
}
