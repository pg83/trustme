//@ proc-macro-aux-build: all_proc_macro_declarations.rs
use all_proc_macro_declarations::{number, passthrough, Mark};

#[derive(Mark)]
struct Label;

#[passthrough]
fn answer() -> u16 { number!() }

fn main() { assert_eq!((answer(), Label::value()), (37, 11)); }
