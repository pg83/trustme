// Extracted from src/safe-unsafe-meaning.md:117
#![allow(unused)]
fn main() {
    use std::cmp::Ordering;
    
    unsafe trait UnsafeOrd {
        fn cmp(&self, other: &Self) -> Ordering;
    }
}
