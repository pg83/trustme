// Extracted from library/core/src/iter/adapters/peekable.rs:280
#![allow(unused)]
fn main() {
    let mut iter = (1..20).peekable();
    // Consume all numbers less than 10
    while iter.next_if(|&x| x < 10).is_some() {}
    // The next value returned will be 10
    assert_eq!(iter.next(), Some(10));
}
