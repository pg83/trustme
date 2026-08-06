// Extracted from library/core/src/num/int_macros.rs:2960
#![allow(unused)]
fn main() {
    let b = 4;
    
    assert_eq!(a.div_euclid(b), 1); // 7 >= 4 * 1
    assert_eq!(a.div_euclid(-b), -1); // 7 >= -4 * -1
    assert_eq!((-a).div_euclid(b), -2); // -7 >= 4 * -2
    assert_eq!((-a).div_euclid(-b), 2); // -7 >= -4 * 2
}
