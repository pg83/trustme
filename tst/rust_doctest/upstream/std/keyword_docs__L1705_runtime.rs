// Extracted from library/std/src/keyword_docs.rs:1705
#![allow(unused)]
fn main() {
    trait MaybeFrom<T> {
        fn maybe_from(value: T) -> Option<Self>
        where
            Self: Sized;
    }
}
