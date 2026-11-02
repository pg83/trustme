//@ compile-fail: Type mismatch
#![feature(unsized_tuple_coercion)]
const PATTERN: &(u16, [u8]) = &(17, [23, 29]);
fn recognizes(value: &(u16, [u8])) -> bool {
    match value { PATTERN => true, _ => false }
}
fn main() {
    assert!(recognizes(&(17, [23, 29])));
    assert!(!recognizes(&(17, [23, 31])));
    assert!(!recognizes(&(19, [23, 29])));
}
