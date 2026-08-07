// Extracted from library/core/src/time.rs:683
#![allow(unused)]
fn main() {
    use std::time::Duration;

    assert_eq!(Duration::new(0, 1).checked_sub(Duration::new(0, 0)), Some(Duration::new(0, 1)));
    assert_eq!(Duration::new(0, 0).checked_sub(Duration::new(0, 1)), None);
}
