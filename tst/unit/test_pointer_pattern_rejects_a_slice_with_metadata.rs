//@ compile-fail: Only integer-derived thin pointers can be used in patterns
const SLICE: *const [u8] = std::ptr::slice_from_raw_parts(std::ptr::null(), 3);
fn classify(pointer: *const [u8]) -> u8 { match pointer { SLICE => 7, _ => 11 } }
fn main() {}
