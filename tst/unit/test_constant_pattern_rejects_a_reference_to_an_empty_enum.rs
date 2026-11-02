//@ compile-fail: uninhabited value in constant pattern
#[derive(PartialEq, Eq)]
enum Empty {}
const INVALID: &Empty = unsafe { core::mem::transmute(1usize) };
fn classify(value: &Empty) -> bool { matches!(value, INVALID) }
fn main() { let _ = classify as fn(&Empty) -> bool; }
