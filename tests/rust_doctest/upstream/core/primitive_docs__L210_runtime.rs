// Extracted from library/core/src/primitive_docs.rs:210
#![allow(unused)]
fn main() {
    use std::ops::Add;
    
    fn foo() -> impl Add<u32> {
        if true {
            unimplemented!()
        } else {
            0
        }
    }
}
