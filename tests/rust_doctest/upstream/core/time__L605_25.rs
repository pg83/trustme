// Extracted from library/core/src/time.rs:605
#![allow(unused)]
fn main() {
    use std::time::Duration;
    
    assert_eq!(Duration::new(100, 0).abs_diff(Duration::new(80, 0)), Duration::new(20, 0));
    assert_eq!(Duration::new(100, 400_000_000).abs_diff(Duration::new(110, 0)), Duration::new(9, 600_000_000));
}
