// Extracted from library/core/src/option.rs:1593
#![allow(unused)]
fn main() {
    let x = Some(2);
    let y = None;
    assert_eq!(x.or(y), Some(2));
    
    let x = None;
    let y = Some(100);
    assert_eq!(x.or(y), Some(100));
    
    let x = Some(2);
    let y = Some(100);
    assert_eq!(x.or(y), Some(2));
    
    let x: Option<u32> = None;
    let y = None;
    assert_eq!(x.or(y), None);
}
