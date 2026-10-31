//@ compile-fail: Only integer-derived thin pointers can be used in patterns
static BYTE: u8 = 37;
const ADDRESS: *const u8 = &BYTE;
fn classify(pointer: *const u8) -> u8 { match pointer { ADDRESS => 7, _ => 11 } }
fn main() {}
