// Extracted from library/core/src/time.rs:105
#![allow(unused)]
#![feature(duration_constants)]
fn main() {
    use std::time::Duration;

    assert_eq!(Duration::MILLISECOND, Duration::from_millis(1));
}
