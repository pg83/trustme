// Extracted from library/std/src/num/f32.rs:36
#![allow(unused)]
fn main() {
    let f = 3.7_f32;
    let g = 3.0_f32;
    let h = -3.7_f32;

    assert_eq!(f.floor(), 3.0);
    assert_eq!(g.floor(), 3.0);
    assert_eq!(h.floor(), -4.0);
}
