// Extracted from library/core/src/result.rs:868
#![allow(unused)]
#![feature(result_option_map_or_default)]
fn main() {
    
    let x: Result<_, &str> = Ok("foo");
    let y: Result<&str, _> = Err("bar");
    
    assert_eq!(x.map_or_default(|x| x.len()), 3);
    assert_eq!(y.map_or_default(|y| y.len()), 0);
}
