//@ compile-fail: Array size out of bounds
type TooLong = [(); 18446744073709551616];
fn main() { let _ = core::mem::size_of::<TooLong>(); }
