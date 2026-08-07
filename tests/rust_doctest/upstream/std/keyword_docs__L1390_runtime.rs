// Extracted from library/std/src/keyword_docs.rs:1390
#![allow(unused)]
#![allow(dead_code)]
fn main() {
    struct Wrap<T> {
        elem: T,
    }

    impl<T> Wrap<T> {
        fn new(elem: T) -> Self {
            Self { elem }
        }
    }
}
