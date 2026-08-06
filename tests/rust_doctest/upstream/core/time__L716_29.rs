// Extracted from library/core/src/time.rs:716
#![allow(unused)]
fn main() {
    use std::time::Duration;
    
    assert_eq!(Duration::new(0, 1).saturating_sub(Duration::new(0, 0)), Duration::new(0, 1));
    assert_eq!(Duration::new(0, 0).saturating_sub(Duration::new(0, 1)), Duration::ZERO);
}
