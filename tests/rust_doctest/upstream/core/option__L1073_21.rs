// Extracted from library/core/src/option.rs:1073
#![allow(unused)]
fn main() {
    let x: Option<u32> = None;
    let y: Option<u32> = Some(12);
    
    assert_eq!(x.unwrap_or_default(), 0);
    assert_eq!(y.unwrap_or_default(), 12);
}
