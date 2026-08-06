// Extracted from library/core/src/num/f128.rs:1631
#![allow(unused)]
#![feature(f128)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f128_math)] {
    
    let m = 10.0_f128;
    let x = 4.0_f128;
    let b = 60.0_f128;
    
    assert_eq!(m.mul_add(x, b), 100.0);
    assert_eq!(m * x + b, 100.0);
    
    let one_plus_eps = 1.0_f128 + f128::EPSILON;
    let one_minus_eps = 1.0_f128 - f128::EPSILON;
    let minus_one = -1.0_f128;
    
    // The exact result (1 + eps) * (1 - eps) = 1 - eps * eps.
    assert_eq!(one_plus_eps.mul_add(one_minus_eps, minus_one), -f128::EPSILON * f128::EPSILON);
    // Different rounding with the non-fused multiply and add.
    assert_eq!(one_plus_eps * one_minus_eps + minus_one, 0.0);
    }
}
