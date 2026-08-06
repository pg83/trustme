// Extracted from library/std/src/keyword_docs.rs:1867
#![allow(unused)]
fn main() {
    type Meters = u32;
    type Kilograms = u32;
    
    let m: Meters = 3;
    let k: Kilograms = 3;
    
    assert_eq!(m, k);
}
