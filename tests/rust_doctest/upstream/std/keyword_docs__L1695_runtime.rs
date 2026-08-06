// Extracted from library/std/src/keyword_docs.rs:1695
#![allow(unused)]
fn main() {
    trait Builder {
        type Built;
    
        fn build(&self) -> Self::Built;
    }
}
