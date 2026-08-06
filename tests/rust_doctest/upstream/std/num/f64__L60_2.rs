// Extracted from library/std/src/num/f64.rs:60
#![allow(unused)]
fn main() {
    let f = 3.01_f64;
    let g = 4.0_f64;
    
    assert_eq!(f.ceil(), 4.0);
    assert_eq!(g.ceil(), 4.0);
}
