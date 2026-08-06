// Extracted from library/core/src/iter/traits/iterator.rs:3370
#![allow(unused)]
fn main() {
    let a = [(1, 2), (3, 4), (5, 6)];
    
    let (left, right): (Vec<_>, Vec<_>) = a.into_iter().unzip();
    
    assert_eq!(left, [1, 3, 5]);
    assert_eq!(right, [2, 4, 6]);
    
    // you can also unzip multiple nested tuples at once
    let a = [(1, (2, 3)), (4, (5, 6))];
    
    let (x, (y, z)): (Vec<_>, (Vec<_>, Vec<_>)) = a.into_iter().unzip();
    assert_eq!(x, [1, 4]);
    assert_eq!(y, [2, 5]);
    assert_eq!(z, [3, 6]);
}
