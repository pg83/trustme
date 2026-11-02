//@ compile-fail: evaluation panicked during constant evaluation
#[inline(never)]
const fn shifted(value: u16, count: u32) -> u16 { value << count }
const INVALID: u16 = shifted(17, 16);
fn main() { let _ = INVALID; }
