// Extracted from library/core/src/iter/traits/iterator.rs:2623
#![allow(unused)]
fn main() {
    let reduced: i32 = (1..10).reduce(|acc, e| acc + e).unwrap_or(0);
    assert_eq!(reduced, 45);
    
    // Which is equivalent to doing it with `fold`:
    let folded: i32 = (1..10).fold(0, |acc, e| acc + e);
    assert_eq!(reduced, folded);
}
