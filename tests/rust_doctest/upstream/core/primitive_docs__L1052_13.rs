// Extracted from library/core/src/primitive_docs.rs:1052
#![allow(unused)]
fn main() {
    let tuple = ("hello", 5, 'c');

    assert_eq!(tuple.0, "hello");
    assert_eq!(tuple.1, 5);
    assert_eq!(tuple.2, 'c');
}
