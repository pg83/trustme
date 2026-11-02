//@ compile-fail: evaluation panicked during constant evaluation
#[inline(never)]
const fn divided(value: i32, divisor: i32) -> i32 { value / divisor }
const INVALID: i32 = divided(i32::MIN, -1);
fn main() { let _ = INVALID; }
