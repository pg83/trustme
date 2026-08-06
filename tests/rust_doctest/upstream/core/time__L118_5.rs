// Extracted from library/core/src/time.rs:118
#![allow(unused)]
#![feature(duration_constants)]
fn main() {
    use std::time::Duration;
    
    assert_eq!(Duration::MICROSECOND, Duration::from_micros(1));
}
