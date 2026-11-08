//@ proc-macro-aux-build: self_path_tokens.rs
#![feature(generic_assert)]
extern crate self_path_tokens as tokens;
macro_rules! forwarded { ($value:expr) => { tokens::same!($value) }; }
#[derive(Debug, PartialEq)]
struct Label(u8);
impl Label {
    fn check(&self) {
        assert!(self == self);
        let echoed = forwarded!(self);
        assert_eq!(echoed.0, self.0);
    }
}
fn main() { Label(7).check(); }
