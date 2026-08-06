// Extracted from library/core/src/array/iter.rs:163
#![allow(unused)]
#![feature(array_into_iter_constructors)]
fn main() {
    use std::array::IntoIter;
    
    let empty = IntoIter::<i32, 3>::empty();
    assert_eq!(empty.len(), 0);
    assert_eq!(empty.as_slice(), &[]);
    
    let empty = IntoIter::<std::convert::Infallible, 200>::empty();
    assert_eq!(empty.len(), 0);
}
