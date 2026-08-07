// Extracted from library/core/src/time.rs:549
#![allow(unused)]
fn main() {
    use std::time::Duration;

    let duration = Duration::new(5, 730_023_852);
    assert_eq!(duration.as_millis(), 5_730);
}
