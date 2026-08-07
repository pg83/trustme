// Extracted from library/core/src/time.rs:57
#![allow(unused)]
fn main() {
    use std::time::Duration;

    let five_seconds = Duration::new(5, 0);
    let five_seconds_and_five_nanos = five_seconds + Duration::new(0, 5);

    assert_eq!(five_seconds_and_five_nanos.as_secs(), 5);
    assert_eq!(five_seconds_and_five_nanos.subsec_nanos(), 5);

    let ten_millis = Duration::from_millis(10);
}
