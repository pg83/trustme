// Extracted from library/core/src/time.rs:1556
#![allow(unused)]
fn main() {
    use std::time::Duration;
    
    let res = Duration::try_from_secs_f32(0.0);
    assert_eq!(res, Ok(Duration::new(0, 0)));
    let res = Duration::try_from_secs_f32(1e-20);
    assert_eq!(res, Ok(Duration::new(0, 0)));
    let res = Duration::try_from_secs_f32(4.2e-7);
    assert_eq!(res, Ok(Duration::new(0, 420)));
    let res = Duration::try_from_secs_f32(2.7);
    assert_eq!(res, Ok(Duration::new(2, 700_000_048)));
    let res = Duration::try_from_secs_f32(3e10);
    assert_eq!(res, Ok(Duration::new(30_000_001_024, 0)));
    // subnormal float:
    let res = Duration::try_from_secs_f32(f32::from_bits(1));
    assert_eq!(res, Ok(Duration::new(0, 0)));
    
    let res = Duration::try_from_secs_f32(-5.0);
    assert!(res.is_err());
    let res = Duration::try_from_secs_f32(f32::NAN);
    assert!(res.is_err());
    let res = Duration::try_from_secs_f32(2e19);
    assert!(res.is_err());
    
    // the conversion uses rounding with tie resolution to even
    let res = Duration::try_from_secs_f32(0.999e-9);
    assert_eq!(res, Ok(Duration::new(0, 1)));
    
    // this float represents exactly 976562.5e-9
    let val = f32::from_bits(0x3A80_0000);
    let res = Duration::try_from_secs_f32(val);
    assert_eq!(res, Ok(Duration::new(0, 976_562)));
    
    // this float represents exactly 2929687.5e-9
    let val = f32::from_bits(0x3B40_0000);
    let res = Duration::try_from_secs_f32(val);
    assert_eq!(res, Ok(Duration::new(0, 2_929_688)));
    
    // this float represents exactly 1.000_976_562_5
    let val = f32::from_bits(0x3F802000);
    let res = Duration::try_from_secs_f32(val);
    assert_eq!(res, Ok(Duration::new(1, 976_562)));
    
    // this float represents exactly 1.002_929_687_5
    let val = f32::from_bits(0x3F806000);
    let res = Duration::try_from_secs_f32(val);
    assert_eq!(res, Ok(Duration::new(1, 2_929_688)));
}
