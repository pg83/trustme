// Extracted from library/core/src/iter/traits/iterator.rs:3154
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];
    let b: [u32; 0] = [];
    
    assert_eq!(a.into_iter().max(), Some(3));
    assert_eq!(b.into_iter().max(), None);
}
