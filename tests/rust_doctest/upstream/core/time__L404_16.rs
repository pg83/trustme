// Extracted from library/core/src/time.rs:404
#![allow(unused)]
#![feature(duration_constructors_lite)]
fn main() {
    use std::time::Duration;

    let duration = Duration::from_mins(10);

    assert_eq!(10 * 60, duration.as_secs());
    assert_eq!(0, duration.subsec_nanos());
}
