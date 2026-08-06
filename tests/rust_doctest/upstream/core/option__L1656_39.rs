// Extracted from library/core/src/option.rs:1656
#![allow(unused)]
fn main() {
    let x = Some(2);
    let y: Option<u32> = None;
    assert_eq!(x.xor(y), Some(2));
    
    let x: Option<u32> = None;
    let y = Some(2);
    assert_eq!(x.xor(y), Some(2));
    
    let x = Some(2);
    let y = Some(2);
    assert_eq!(x.xor(y), None);
    
    let x: Option<u32> = None;
    let y: Option<u32> = None;
    assert_eq!(x.xor(y), None);
}
