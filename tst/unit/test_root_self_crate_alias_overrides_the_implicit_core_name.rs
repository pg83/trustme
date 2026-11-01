extern crate self as core;
pub struct Token(pub u16);

fn main() {
    let token = core::Token(19);
    assert_eq!(token.0, 19);
}
