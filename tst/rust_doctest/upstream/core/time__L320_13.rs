// Extracted from library/core/src/time.rs:320
#![allow(unused)]
#![feature(duration_constructors)]
fn main() {
    use std::time::Duration;

    let duration = Duration::from_weeks(4);

    assert_eq!(4 * 7 * 24 * 60 * 60, duration.as_secs());
    assert_eq!(0, duration.subsec_nanos());
}
