// Extracted from library/core/src/num/f32.rs:706
#![allow(unused)]
fn main() {
    let f = 7.0f32;
    let g = -7.0f32;
    
    assert!(!f.is_sign_negative());
    assert!(g.is_sign_negative());
}
