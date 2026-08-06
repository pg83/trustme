// Extracted from library/alloc/src/boxed/convert.rs:32
#![allow(unused)]
extern crate alloc;
fn main() {
    let x = 5;
    let boxed = Box::new(5);
    
    assert_eq!(Box::from(x), boxed);
}
