// Extracted from library/core/src/result.rs:1464
#![allow(unused)]
fn main() {
    fn sq(x: u32) -> Result<u32, u32> { Ok(x * x) }
    fn err(x: u32) -> Result<u32, u32> { Err(x) }
    
    assert_eq!(Ok(2).or_else(sq).or_else(sq), Ok(2));
    assert_eq!(Ok(2).or_else(err).or_else(sq), Ok(2));
    assert_eq!(Err(3).or_else(sq).or_else(err), Ok(9));
    assert_eq!(Err(3).or_else(err).or_else(err), Err(3));
}
