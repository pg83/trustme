// Extracted from library/core/src/time.rs:905
#![allow(unused)]
fn main() {
    use std::time::Duration;
    
    let res = Duration::from_secs_f64(0.0);
    assert_eq!(res, Duration::new(0, 0));
    let res = Duration::from_secs_f64(1e-20);
    assert_eq!(res, Duration::new(0, 0));
    let res = Duration::from_secs_f64(4.2e-7);
    assert_eq!(res, Duration::new(0, 420));
    let res = Duration::from_secs_f64(2.7);
    assert_eq!(res, Duration::new(2, 700_000_000));
    let res = Duration::from_secs_f64(3e10);
    assert_eq!(res, Duration::new(30_000_000_000, 0));
    // subnormal float
    let res = Duration::from_secs_f64(f64::from_bits(1));
    assert_eq!(res, Duration::new(0, 0));
    // conversion uses rounding
    let res = Duration::from_secs_f64(0.999e-9);
    assert_eq!(res, Duration::new(0, 1));
}
