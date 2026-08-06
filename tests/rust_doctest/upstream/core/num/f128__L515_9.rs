// Extracted from library/core/src/num/f128.rs:515
#![allow(unused)]
#![feature(f128)]
fn main() {
    // FIXME(f16_f128): remove when `eqtf2` is available
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    // f128::EPSILON is the difference between 1.0 and the next number up.
    assert_eq!(1.0f128.next_up(), 1.0 + f128::EPSILON);
    // But not for most numbers.
    assert!(0.1f128.next_up() < 0.1 + f128::EPSILON);
    assert_eq!(4611686018427387904f128.next_up(), 4611686018427387904.000000000000001);
    }
}
