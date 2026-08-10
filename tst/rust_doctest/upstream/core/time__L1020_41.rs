// Extracted from library/core/src/time.rs:1020
#![allow(unused)]
fn main() {
    use std::time::Duration;

    let dur = Duration::new(2, 700_000_000);
    assert_eq!(dur.div_f64(3.14), Duration::new(0, 859_872_611));
    assert_eq!(dur.div_f64(3.14e5), Duration::new(0, 8_599));
}
