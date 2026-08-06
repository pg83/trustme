// Extracted from library/core/src/iter/sources/repeat.rs:26
#![allow(unused)]
fn main() {
    use std::iter;
    
    // the number four 4ever:
    let mut fours = iter::repeat(4);
    
    assert_eq!(Some(4), fours.next());
    assert_eq!(Some(4), fours.next());
    assert_eq!(Some(4), fours.next());
    assert_eq!(Some(4), fours.next());
    assert_eq!(Some(4), fours.next());
    
    // yup, still four
    assert_eq!(Some(4), fours.next());
}
