// Extracted from library/core/src/iter/traits/iterator.rs:2195
#![allow(unused)]
fn main() {
    let a = [1, 2, 3];
    
    let (even, odd): (Vec<_>, Vec<_>) = a
        .into_iter()
        .partition(|n| n % 2 == 0);
    
    assert_eq!(even, [2]);
    assert_eq!(odd, [1, 3]);
}
