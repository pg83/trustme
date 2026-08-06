// Extracted from library/std/src/num/f64.rs:140
#![allow(unused)]
fn main() {
    let f = 3.7_f64;
    let g = 3.0_f64;
    let h = -3.7_f64;
    
    assert_eq!(f.trunc(), 3.0);
    assert_eq!(g.trunc(), 3.0);
    assert_eq!(h.trunc(), -3.0);
}
