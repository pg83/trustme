// Extracted from library/core/src/option.rs:2694
#![allow(unused)]
#![feature(option_array_transpose)]
fn main() {
    use std::option::Option;
    
    let data = [Some(0); 1000];
    let data: Option<[u8; 1000]> = data.transpose();
    assert_eq!(data, Some([0; 1000]));
    
    let data = [Some(0), None];
    let data: Option<[u8; 2]> = data.transpose();
    assert_eq!(data, None);
}
