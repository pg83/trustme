// Extracted from library/core/src/time.rs:213
#![allow(unused)]
fn main() {
    use std::time::Duration;

    let duration = Duration::from_secs(5);

    assert_eq!(5, duration.as_secs());
    assert_eq!(0, duration.subsec_nanos());
}
