// Extracted from library/core/src/iter/traits/iterator.rs:1413
#![allow(unused)]
fn main() {
    let a = [1, 2, 3, 4];
    
    let mut iter = a.into_iter().scan(1, |state, x| {
        // each iteration, we'll multiply the state by the element ...
        *state = *state * x;
    
        // ... and terminate if the state exceeds 6
        if *state > 6 {
            return None;
        }
        // ... else yield the negation of the state
        Some(-*state)
    });
    
    assert_eq!(iter.next(), Some(-1));
    assert_eq!(iter.next(), Some(-2));
    assert_eq!(iter.next(), Some(-6));
    assert_eq!(iter.next(), None);
}
