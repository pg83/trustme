// Extracted from library/core/src/time.rs:484
#![allow(unused)]
fn main() {
    use std::time::Duration;
    
    let duration = Duration::from_millis(5_432);
    assert_eq!(duration.as_secs(), 5);
    assert_eq!(duration.subsec_millis(), 432);
}
