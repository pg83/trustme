// Extracted from library/std/src/keyword_docs.rs:2345
#![allow(unused)]
fn main() {
    fn first_or_default<I>(mut i: I) -> I::Item
    where
        I: Iterator,
        I::Item: Default,
    {
        i.next().unwrap_or_else(I::Item::default)
    }
    
    assert_eq!(first_or_default([1, 2, 3].into_iter()), 1);
    assert_eq!(first_or_default(Vec::<i32>::new().into_iter()), 0);
}
