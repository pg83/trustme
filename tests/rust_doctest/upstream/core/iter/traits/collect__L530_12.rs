// Extracted from library/core/src/iter/traits/collect.rs:530
#![allow(unused)]
fn main() {
    // Example given for a 2-tuple, but 1- through 12-tuples are supported
    let mut tuple = (vec![0], vec![1]);
    tuple.extend([(2, 3), (4, 5), (6, 7)]);
    assert_eq!(tuple.0, [0, 2, 4, 6]);
    assert_eq!(tuple.1, [1, 3, 5, 7]);
    
    // also allows for arbitrarily nested tuples as elements
    let mut nested_tuple = (vec![1], (vec![2], vec![3]));
    nested_tuple.extend([(4, (5, 6)), (7, (8, 9))]);
    
    let (a, (b, c)) = nested_tuple;
    assert_eq!(a, [1, 4, 7]);
    assert_eq!(b, [2, 5, 8]);
    assert_eq!(c, [3, 6, 9]);
}
