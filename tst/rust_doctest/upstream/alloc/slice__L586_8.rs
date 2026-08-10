// Extracted from library/alloc/src/slice.rs:586
#![allow(unused)]
extern crate alloc;
fn main() {
    assert_eq!(["hello", "world"].join(" "), "hello world");
    assert_eq!([[1, 2], [3, 4]].join(&0), [1, 2, 0, 3, 4]);
    assert_eq!([[1, 2], [3, 4]].join(&[0, 0][..]), [1, 2, 0, 0, 3, 4]);
}
