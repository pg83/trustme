// Extracted from library/core/src/option.rs:618
#![allow(unused)]
fn main() {
    let x: Option<u32> = Some(2);
    assert_eq!(x.is_some(), true);
    
    let x: Option<u32> = None;
    assert_eq!(x.is_some(), false);
}
