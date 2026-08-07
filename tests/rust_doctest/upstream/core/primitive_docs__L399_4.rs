// Extracted from library/core/src/primitive_docs.rs:399
#![allow(unused)]
fn main() {
    let v = vec!['h', 'e', 'l', 'l', 'o'];

    // five elements times four bytes for each element
    assert_eq!(20, v.len() * size_of::<char>());

    let s = String::from("hello");

    // five elements times one byte per element
    assert_eq!(5, s.len() * size_of::<u8>());
}
