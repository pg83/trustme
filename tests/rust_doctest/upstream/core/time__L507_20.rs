// Extracted from library/core/src/time.rs:507
#![allow(unused)]
fn main() {
    use std::time::Duration;

    let duration = Duration::from_micros(1_234_567);
    assert_eq!(duration.as_secs(), 1);
    assert_eq!(duration.subsec_micros(), 234_567);
}
