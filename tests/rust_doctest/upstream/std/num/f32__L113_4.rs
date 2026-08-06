// Extracted from library/std/src/num/f32.rs:113
#![allow(unused)]
fn main() {
    let f = 3.3_f32;
    let g = -3.3_f32;
    let h = 3.5_f32;
    let i = 4.5_f32;
    
    assert_eq!(f.round_ties_even(), 3.0);
    assert_eq!(g.round_ties_even(), -3.0);
    assert_eq!(h.round_ties_even(), 4.0);
    assert_eq!(i.round_ties_even(), 4.0);
}
