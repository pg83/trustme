// Extracted from library/core/src/time.rs:92
#![allow(unused)]
#![feature(duration_constants)]
fn main() {
    use std::time::Duration;
    
    assert_eq!(Duration::SECOND, Duration::from_secs(1));
}
