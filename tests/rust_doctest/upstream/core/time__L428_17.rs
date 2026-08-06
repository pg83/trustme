// Extracted from library/core/src/time.rs:428
#![allow(unused)]
fn main() {
    use std::time::Duration;
    
    assert!(Duration::ZERO.is_zero());
    assert!(Duration::new(0, 0).is_zero());
    assert!(Duration::from_nanos(0).is_zero());
    assert!(Duration::from_secs(0).is_zero());
    
    assert!(!Duration::new(1, 1).is_zero());
    assert!(!Duration::from_nanos(1).is_zero());
    assert!(!Duration::from_secs(1).is_zero());
}
