// Extracted from library/alloc/src/vec/mod.rs:3914
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut items = vec![0, 0, 0, 0, 0, 0, 0, 1, 2, 1, 2, 1, 2];
    let ones = items.extract_if(7.., |x| *x == 1).collect::<Vec<_>>();
    assert_eq!(items, vec![0, 0, 0, 0, 0, 0, 0, 2, 2, 2]);
    assert_eq!(ones.len(), 3);
}
