// Extracted from library/core/src/time.rs:530
#![allow(unused)]
fn main() {
    use std::time::Duration;

    let duration = Duration::from_millis(5_010);
    assert_eq!(duration.as_secs(), 5);
    assert_eq!(duration.subsec_nanos(), 10_000_000);
}
