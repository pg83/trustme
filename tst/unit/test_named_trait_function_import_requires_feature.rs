//@ compile-fail: import_trait_associated_functions
trait Source { fn read() -> u16; }
use Source::read;
fn main() {}
