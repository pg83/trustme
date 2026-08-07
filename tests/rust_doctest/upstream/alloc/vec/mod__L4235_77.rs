// Extracted from library/alloc/src/vec/mod.rs:4235
#![allow(unused)]
extern crate alloc;
fn main() {
    let r: Result<[i32; 4], _> = (0..10).collect::<Vec<_>>().try_into();
    assert_eq!(r, Err(vec![0, 1, 2, 3, 4, 5, 6, 7, 8, 9]));
}
