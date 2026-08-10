// Extracted from library/core/src/time.rs:233
#![allow(unused)]
fn main() {
    use std::time::Duration;

    let duration = Duration::from_millis(2_569);

    assert_eq!(2, duration.as_secs());
    assert_eq!(569_000_000, duration.subsec_nanos());
}
