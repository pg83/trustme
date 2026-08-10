// Extracted from library/core/src/time.rs:144
#![allow(unused)]
fn main() {
    use std::time::Duration;

    let duration = Duration::ZERO;
    assert!(duration.is_zero());
    assert_eq!(duration.as_nanos(), 0);
}
