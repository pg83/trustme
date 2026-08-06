// Extracted from library/core/src/num/f64.rs:906
#![allow(unused)]
fn main() {
    let x = 1.0_f64;
    let y = 2.0_f64;
    
    assert_eq!(x.max(y), y);
}
