// Extracted from library/core/src/iter/traits/collect.rs:171
#![allow(unused)]
fn main() {
    let v = [1, 2, 3];
    let mut iter = v.into_iter();
    
    assert_eq!(Some(1), iter.next());
    assert_eq!(Some(2), iter.next());
    assert_eq!(Some(3), iter.next());
    assert_eq!(None, iter.next());
}
