// Extracted from library/std/src/keyword_docs.rs:2288
#![allow(unused)]
fn main() {
    fn new<T: Default>() -> T {
        T::default()
    }

    fn new_where<T>() -> T
    where
        T: Default,
    {
        T::default()
    }

    assert_eq!(0.0, new());
    assert_eq!(0.0, new_where());

    assert_eq!(0, new());
    assert_eq!(0, new_where());
}
