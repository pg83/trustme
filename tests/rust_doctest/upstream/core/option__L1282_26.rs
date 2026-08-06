// Extracted from library/core/src/option.rs:1282
#![allow(unused)]
#![feature(result_option_map_or_default)]
fn main() {
    
    let x: Option<&str> = Some("hi");
    let y: Option<&str> = None;
    
    assert_eq!(x.map_or_default(|x| x.len()), 2);
    assert_eq!(y.map_or_default(|y| y.len()), 0);
}
