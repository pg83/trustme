// Extracted from library/core/src/time.rs:568
#![allow(unused)]
fn main() {
    use std::time::Duration;

    let duration = Duration::new(5, 730_023_852);
    assert_eq!(duration.as_micros(), 5_730_023);
}
