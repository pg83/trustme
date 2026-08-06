// Extracted from library/core/src/num/int_macros.rs:2997
#![allow(unused)]
fn main() {
    let b = 4;
    
    assert_eq!(a.rem_euclid(b), 3);
    assert_eq!((-a).rem_euclid(b), 1);
    assert_eq!(a.rem_euclid(-b), 3);
    assert_eq!((-a).rem_euclid(-b), 1);
}
