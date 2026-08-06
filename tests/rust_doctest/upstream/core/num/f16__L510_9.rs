// Extracted from library/core/src/num/f16.rs:510
#![allow(unused)]
#![feature(f16)]
fn main() {
    // FIXME(f16_f128): ABI issues on MSVC
    #[cfg(all(target_arch = "x86_64", target_os = "linux"))] {
    
    // f16::EPSILON is the difference between 1.0 and the next number up.
    assert_eq!(1.0f16.next_up(), 1.0 + f16::EPSILON);
    // But not for most numbers.
    assert!(0.1f16.next_up() < 0.1 + f16::EPSILON);
    assert_eq!(4356f16.next_up(), 4360.0);
    }
}
