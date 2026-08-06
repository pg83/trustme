// Extracted from library/core/src/time.rs:1083
#![allow(unused)]
fn main() {
    use std::time::Duration;
    
    let dur1 = Duration::new(2, 700_000_000);
    let dur2 = Duration::new(5, 400_000_000);
    assert_eq!(dur1.div_duration_f32(dur2), 0.5);
}
