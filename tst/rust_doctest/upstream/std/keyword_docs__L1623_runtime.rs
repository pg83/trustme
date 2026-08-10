// Extracted from library/std/src/keyword_docs.rs:1623
#![allow(dead_code)]
fn main() {}
mod a {
    pub fn foo() {}
}
mod b {
    pub fn foo() {
        super::a::foo(); // call a's foo function
    }
}
