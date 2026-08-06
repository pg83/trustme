// Extracted from library/std/src/keyword_docs.rs:1790
#![allow(unused)]
fn main() {
    trait A {}
    
    let _: Box<dyn A + Send + Sync>;
}
