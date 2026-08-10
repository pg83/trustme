// Extracted from library/core/src/time.rs:131
#![allow(unused)]
#![feature(duration_constants)]
fn main() {
    use std::time::Duration;

    assert_eq!(Duration::NANOSECOND, Duration::from_nanos(1));
}
