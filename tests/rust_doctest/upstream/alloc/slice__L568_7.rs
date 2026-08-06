// Extracted from library/alloc/src/slice.rs:568
#![allow(unused)]
extern crate alloc;
fn main() {
    assert_eq!(["hello", "world"].concat(), "helloworld");
    assert_eq!([[1, 2], [3, 4]].concat(), [1, 2, 3, 4]);
}
