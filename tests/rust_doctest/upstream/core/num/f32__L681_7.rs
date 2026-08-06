// Extracted from library/core/src/num/f32.rs:681
#![allow(unused)]
fn main() {
    let f = 7.0_f32;
    let g = -7.0_f32;
    
    assert!(f.is_sign_positive());
    assert!(!g.is_sign_positive());
}
