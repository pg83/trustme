// Extracted from library/core/src/time.rs:348
#![allow(unused)]
#![feature(duration_constructors)]
fn main() {
    use std::time::Duration;
    
    let duration = Duration::from_days(7);
    
    assert_eq!(7 * 24 * 60 * 60, duration.as_secs());
    assert_eq!(0, duration.subsec_nanos());
}
