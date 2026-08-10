// Extracted from library/core/src/time.rs:290
#![allow(unused)]
fn main() {
    use std::time::Duration;

    let duration = Duration::from_nanos(1_000_000_123);

    assert_eq!(1, duration.as_secs());
    assert_eq!(123, duration.subsec_nanos());
}
