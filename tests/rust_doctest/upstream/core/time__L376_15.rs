// Extracted from library/core/src/time.rs:376
#![allow(unused)]
#![feature(duration_constructors_lite)]
fn main() {
    use std::time::Duration;

    let duration = Duration::from_hours(6);

    assert_eq!(6 * 60 * 60, duration.as_secs());
    assert_eq!(0, duration.subsec_nanos());
}
