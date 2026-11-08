//@ proc-macro-aux-build: echo_literal_tokens.rs
use echo_literal_tokens::echo;

macro_rules! forward {
    ($value:expr) => { echo!($value) };
}

fn main() {
    assert_eq!(forward!("a\nβ\""), "a\nβ\"");
    assert_eq!(forward!(b"\0\xff\""), b"\0\xff\"");
    assert_eq!(forward!(c"c\n\"").to_bytes(), b"c\n\"");
}
