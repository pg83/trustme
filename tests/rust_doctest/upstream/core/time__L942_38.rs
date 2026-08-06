// Extracted from library/core/src/time.rs:942
#![allow(unused)]
fn main() {
    use std::time::Duration;
    
    let res = Duration::from_secs_f32(0.0);
    assert_eq!(res, Duration::new(0, 0));
    let res = Duration::from_secs_f32(1e-20);
    assert_eq!(res, Duration::new(0, 0));
    let res = Duration::from_secs_f32(4.2e-7);
    assert_eq!(res, Duration::new(0, 420));
    let res = Duration::from_secs_f32(2.7);
    assert_eq!(res, Duration::new(2, 700_000_048));
    let res = Duration::from_secs_f32(3e10);
    assert_eq!(res, Duration::new(30_000_001_024, 0));
    // subnormal float
    let res = Duration::from_secs_f32(f32::from_bits(1));
    assert_eq!(res, Duration::new(0, 0));
    // conversion uses rounding
    let res = Duration::from_secs_f32(0.999e-9);
    assert_eq!(res, Duration::new(0, 1));
}
