// Extracted from library/core/src/time.rs:1624
#![allow(unused)]
fn main() {
    use std::time::Duration;
    
    let res = Duration::try_from_secs_f64(0.0);
    assert_eq!(res, Ok(Duration::new(0, 0)));
    let res = Duration::try_from_secs_f64(1e-20);
    assert_eq!(res, Ok(Duration::new(0, 0)));
    let res = Duration::try_from_secs_f64(4.2e-7);
    assert_eq!(res, Ok(Duration::new(0, 420)));
    let res = Duration::try_from_secs_f64(2.7);
    assert_eq!(res, Ok(Duration::new(2, 700_000_000)));
    let res = Duration::try_from_secs_f64(3e10);
    assert_eq!(res, Ok(Duration::new(30_000_000_000, 0)));
    // subnormal float
    let res = Duration::try_from_secs_f64(f64::from_bits(1));
    assert_eq!(res, Ok(Duration::new(0, 0)));
    
    let res = Duration::try_from_secs_f64(-5.0);
    assert!(res.is_err());
    let res = Duration::try_from_secs_f64(f64::NAN);
    assert!(res.is_err());
    let res = Duration::try_from_secs_f64(2e19);
    assert!(res.is_err());
    
    // the conversion uses rounding with tie resolution to even
    let res = Duration::try_from_secs_f64(0.999e-9);
    assert_eq!(res, Ok(Duration::new(0, 1)));
    let res = Duration::try_from_secs_f64(0.999_999_999_499);
    assert_eq!(res, Ok(Duration::new(0, 999_999_999)));
    let res = Duration::try_from_secs_f64(0.999_999_999_501);
    assert_eq!(res, Ok(Duration::new(1, 0)));
    let res = Duration::try_from_secs_f64(42.999_999_999_499);
    assert_eq!(res, Ok(Duration::new(42, 999_999_999)));
    let res = Duration::try_from_secs_f64(42.999_999_999_501);
    assert_eq!(res, Ok(Duration::new(43, 0)));
    
    // this float represents exactly 976562.5e-9
    let val = f64::from_bits(0x3F50_0000_0000_0000);
    let res = Duration::try_from_secs_f64(val);
    assert_eq!(res, Ok(Duration::new(0, 976_562)));
    
    // this float represents exactly 2929687.5e-9
    let val = f64::from_bits(0x3F68_0000_0000_0000);
    let res = Duration::try_from_secs_f64(val);
    assert_eq!(res, Ok(Duration::new(0, 2_929_688)));
    
    // this float represents exactly 1.000_976_562_5
    let val = f64::from_bits(0x3FF0_0400_0000_0000);
    let res = Duration::try_from_secs_f64(val);
    assert_eq!(res, Ok(Duration::new(1, 976_562)));
    
    // this float represents exactly 1.002_929_687_5
    let val = f64::from_bits(0x3_FF00_C000_0000_000);
    let res = Duration::try_from_secs_f64(val);
    assert_eq!(res, Ok(Duration::new(1, 2_929_688)));
}
