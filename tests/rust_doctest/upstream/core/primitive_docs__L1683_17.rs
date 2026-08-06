// Extracted from library/core/src/primitive_docs.rs:1683
#![allow(unused)]
fn main() {
    fn add_one(x: usize) -> usize {
        x + 1
    }
    
    let ptr: fn(usize) -> usize = add_one;
    assert_eq!(ptr(5), 6);
    
    let clos: fn(usize) -> usize = |x| x + 5;
    assert_eq!(clos(5), 10);
}
