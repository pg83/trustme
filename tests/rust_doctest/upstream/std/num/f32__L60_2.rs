// Extracted from library/std/src/num/f32.rs:60
#![allow(unused)]
fn main() {
    let f = 3.01_f32;
    let g = 4.0_f32;
    
    assert_eq!(f.ceil(), 4.0);
    assert_eq!(g.ceil(), 4.0);
}
