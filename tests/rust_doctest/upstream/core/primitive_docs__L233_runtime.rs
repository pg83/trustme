// Extracted from library/core/src/primitive_docs.rs:233
#![allow(unused)]
#![feature(never_type)]
fn main() {
    use std::fmt;
    trait Debug {
        fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result;
    }
    impl Debug for ! {
        fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
            *self
        }
    }
}
