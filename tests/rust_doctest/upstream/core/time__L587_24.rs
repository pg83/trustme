// Extracted from library/core/src/time.rs:587
#![allow(unused)]
fn main() {
    use std::time::Duration;
    
    let duration = Duration::new(5, 730_023_852);
    assert_eq!(duration.as_nanos(), 5_730_023_852);
}
