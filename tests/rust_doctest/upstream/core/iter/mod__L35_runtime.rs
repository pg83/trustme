// Extracted from library/core/src/iter/mod.rs:35
#![allow(unused)]
fn main() {
    trait Iterator {
        type Item;
        fn next(&mut self) -> Option<Self::Item>;
    }
}
