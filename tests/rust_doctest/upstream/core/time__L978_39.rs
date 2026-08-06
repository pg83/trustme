// Extracted from library/core/src/time.rs:978
#![allow(unused)]
fn main() {
    use std::time::Duration;
    
    let dur = Duration::new(2, 700_000_000);
    assert_eq!(dur.mul_f64(3.14), Duration::new(8, 478_000_000));
    assert_eq!(dur.mul_f64(3.14e5), Duration::new(847_800, 0));
}
