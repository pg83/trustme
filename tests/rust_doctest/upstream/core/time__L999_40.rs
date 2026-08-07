// Extracted from library/core/src/time.rs:999
#![allow(unused)]
fn main() {
    use std::time::Duration;

    let dur = Duration::new(2, 700_000_000);
    assert_eq!(dur.mul_f32(3.14), Duration::new(8, 478_000_641));
    assert_eq!(dur.mul_f32(3.14e5), Duration::new(847_800, 0));
}
