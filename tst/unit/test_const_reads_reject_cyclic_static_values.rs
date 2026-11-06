//@ compile-fail: error
static LEFT: u8 = RIGHT;
static RIGHT: u8 = LEFT;
const VALUE: u8 = LEFT;
fn main() { let _ = VALUE; }
