// Extracted from library/core/src/num/f16.rs:1607
#![allow(unused)]
#![feature(f16)]
fn main() {
    #[cfg(not(miri))]
    #[cfg(target_has_reliable_f16_math)] {
    
    let m = 10.0_f16;
    let x = 4.0_f16;
    let b = 60.0_f16;
    
    assert_eq!(m.mul_add(x, b), 100.0);
    assert_eq!(m * x + b, 100.0);
    
    let one_plus_eps = 1.0_f16 + f16::EPSILON;
    let one_minus_eps = 1.0_f16 - f16::EPSILON;
    let minus_one = -1.0_f16;
    
    // The exact result (1 + eps) * (1 - eps) = 1 - eps * eps.
    assert_eq!(one_plus_eps.mul_add(one_minus_eps, minus_one), -f16::EPSILON * f16::EPSILON);
    // Different rounding with the non-fused multiply and add.
    assert_eq!(one_plus_eps * one_minus_eps + minus_one, 0.0);
    }
}
