// Extracted from src/borrow-splitting.md:90
#![allow(unused)]
fn main() {
    trait Iterator {
        type Item;
    
        fn next(&mut self) -> Option<Self::Item>;
    }
}
