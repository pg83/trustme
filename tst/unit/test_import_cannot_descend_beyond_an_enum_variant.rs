//@ compile-fail: Encountered enum at unexpected location in import
enum Mode { First }
use crate::Mode::First::Missing;
fn main() {}
