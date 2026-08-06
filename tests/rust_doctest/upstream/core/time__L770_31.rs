// Extracted from library/core/src/time.rs:770
#![allow(unused)]
#![feature(duration_constants)]
fn main() {
    use std::time::Duration;
    
    assert_eq!(Duration::new(0, 500_000_001).saturating_mul(2), Duration::new(1, 2));
    assert_eq!(Duration::new(u64::MAX - 1, 0).saturating_mul(2), Duration::MAX);
}
