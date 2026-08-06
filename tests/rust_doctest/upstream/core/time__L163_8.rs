// Extracted from library/core/src/time.rs:163
#![allow(unused)]
fn main() {
    use std::time::Duration;
    
    assert_eq!(Duration::MAX, Duration::new(u64::MAX, 1_000_000_000 - 1));
}
