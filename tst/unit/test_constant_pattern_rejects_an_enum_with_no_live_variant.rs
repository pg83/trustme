//@ compile-fail: uninhabited value in constant pattern
#[derive(PartialEq, Eq)]
enum Empty {}
#[derive(PartialEq, Eq)]
enum Dead { First(Empty), Second { value: Empty } }
const INVALID: &Dead = unsafe { core::mem::transmute(1usize) };
fn classify(value: &Dead) -> bool { matches!(value, INVALID) }
fn main() { let _ = classify as fn(&Dead) -> bool; }
