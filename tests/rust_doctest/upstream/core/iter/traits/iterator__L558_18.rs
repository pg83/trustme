// Extracted from library/core/src/iter/traits/iterator.rs:558
#![allow(unused)]
fn main() {
    let enumerate: Vec<_> = "foo".chars().enumerate().collect();
    
    let zipper: Vec<_> = (0..).zip("foo".chars()).collect();
    
    assert_eq!((0, 'f'), enumerate[0]);
    assert_eq!((0, 'f'), zipper[0]);
    
    assert_eq!((1, 'o'), enumerate[1]);
    assert_eq!((1, 'o'), zipper[1]);
    
    assert_eq!((2, 'o'), enumerate[2]);
    assert_eq!((2, 'o'), zipper[2]);
}
