// Extracted from library/alloc/src/vec/mod.rs:3321
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = vec![[1, 2, 3], [4, 5, 6], [7, 8, 9]];
    assert_eq!(vec.pop(), Some([7, 8, 9]));

    let mut flattened = vec.into_flattened();
    assert_eq!(flattened.pop(), Some(6));
}
