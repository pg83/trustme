// Extracted from library/std/src/num/f64.rs:84
#![allow(unused)]
fn main() {
    let f = 3.3_f64;
    let g = -3.3_f64;
    let h = -3.7_f64;
    let i = 3.5_f64;
    let j = 4.5_f64;
    
    assert_eq!(f.round(), 3.0);
    assert_eq!(g.round(), -3.0);
    assert_eq!(h.round(), -4.0);
    assert_eq!(i.round(), 4.0);
    assert_eq!(j.round(), 5.0);
}
