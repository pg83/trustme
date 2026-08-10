// Extracted from library/core/src/time.rs:659
#![allow(unused)]
#![feature(duration_constants)]
fn main() {
    use std::time::Duration;

    assert_eq!(Duration::new(0, 0).saturating_add(Duration::new(0, 1)), Duration::new(0, 1));
    assert_eq!(Duration::new(1, 0).saturating_add(Duration::new(u64::MAX, 0)), Duration::MAX);
}
