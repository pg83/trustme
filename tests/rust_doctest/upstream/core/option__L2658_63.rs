// Extracted from library/core/src/option.rs:2658
#![allow(unused)]
fn main() {
    let x: Option<Option<u32>> = Some(Some(6));
    assert_eq!(Some(6), x.flatten());
    
    let x: Option<Option<u32>> = Some(None);
    assert_eq!(None, x.flatten());
    
    let x: Option<Option<u32>> = None;
    assert_eq!(None, x.flatten());
}
