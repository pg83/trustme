// Extracted from library/core/src/time.rs:739
#![allow(unused)]
fn main() {
    use std::time::Duration;

    assert_eq!(Duration::new(0, 500_000_001).checked_mul(2), Some(Duration::new(1, 2)));
    assert_eq!(Duration::new(u64::MAX - 1, 0).checked_mul(2), None);
}
