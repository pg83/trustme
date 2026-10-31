//@ compile-fail: Attempting to match over
fn value() -> u16 { 37 }
const KEY: fn() -> u16 = value;
fn classify(function: fn() -> u16) -> bool {
    match function { KEY => true, _ => false }
}
fn main() { let _ = classify(value); }
