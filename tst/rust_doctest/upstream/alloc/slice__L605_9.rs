// Extracted from library/alloc/src/slice.rs:605
#![allow(unused)]
#![allow(deprecated)]
extern crate alloc;
fn main() {
    assert_eq!(["hello", "world"].connect(" "), "hello world");
    assert_eq!([[1, 2], [3, 4]].connect(&0), [1, 2, 0, 3, 4]);
}
