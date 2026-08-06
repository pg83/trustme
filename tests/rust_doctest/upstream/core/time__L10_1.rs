// Extracted from library/core/src/time.rs:10
#![allow(unused)]
fn main() {
    use std::time::Duration;
    let five_seconds = Duration::from_secs(5);
    assert_eq!(five_seconds, Duration::from_millis(5_000));
    assert_eq!(five_seconds, Duration::from_micros(5_000_000));
    assert_eq!(five_seconds, Duration::from_nanos(5_000_000_000));
    
    let ten_seconds = Duration::from_secs(10);
    let seven_nanos = Duration::from_nanos(7);
    let total = ten_seconds + seven_nanos;
    assert_eq!(total, Duration::new(10, 7));
}
