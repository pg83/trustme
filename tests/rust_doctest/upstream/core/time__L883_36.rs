// Extracted from library/core/src/time.rs:883
#![allow(unused)]
#![feature(duration_millis_float)]
fn main() {
    use std::time::Duration;

    let dur = Duration::new(2, 345_678_000);
    assert_eq!(dur.as_millis_f32(), 2_345.678);
}
