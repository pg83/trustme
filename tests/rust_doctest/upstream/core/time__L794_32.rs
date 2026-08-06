// Extracted from library/core/src/time.rs:794
#![allow(unused)]
fn main() {
    use std::time::Duration;
    
    assert_eq!(Duration::new(2, 0).checked_div(2), Some(Duration::new(1, 0)));
    assert_eq!(Duration::new(1, 0).checked_div(2), Some(Duration::new(0, 500_000_000)));
    assert_eq!(Duration::new(2, 0).checked_div(0), None);
}
