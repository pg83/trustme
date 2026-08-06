// Extracted from library/core/src/time.rs:625
#![allow(unused)]
fn main() {
    use std::time::Duration;
    
    assert_eq!(Duration::new(0, 0).checked_add(Duration::new(0, 1)), Some(Duration::new(0, 1)));
    assert_eq!(Duration::new(1, 0).checked_add(Duration::new(u64::MAX, 0)), None);
}
