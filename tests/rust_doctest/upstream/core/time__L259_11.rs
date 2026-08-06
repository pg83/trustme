// Extracted from library/core/src/time.rs:259
#![allow(unused)]
fn main() {
    use std::time::Duration;
    
    let duration = Duration::from_micros(1_000_002);
    
    assert_eq!(1, duration.as_secs());
    assert_eq!(2_000, duration.subsec_nanos());
}
