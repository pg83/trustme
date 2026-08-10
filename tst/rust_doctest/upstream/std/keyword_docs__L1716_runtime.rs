// Extracted from library/std/src/keyword_docs.rs:1716
#![allow(unused)]
fn main() {
    trait ThreeIterator: Iterator {
        fn next_three(&mut self) -> Option<[Self::Item; 3]>;
    }
}
