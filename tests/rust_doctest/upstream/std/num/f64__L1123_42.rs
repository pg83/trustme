// Extracted from library/std/src/num/f64.rs:1123
#![allow(unused)]
fn main() {
    let e = std::f64::consts::E;
    let f = e.tanh().atanh();
    
    let abs_difference = (f - e).abs();
    
    assert!(abs_difference < 1.0e-10);
}
