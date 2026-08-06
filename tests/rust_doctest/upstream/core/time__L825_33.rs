// Extracted from library/core/src/time.rs:825
#![allow(unused)]
fn main() {
    use std::time::Duration;
    
    let dur = Duration::new(2, 700_000_000);
    assert_eq!(dur.as_secs_f64(), 2.7);
}
