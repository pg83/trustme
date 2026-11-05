//@ compile-variant: -O
#![feature(type_alias_impl_trait)]

type Token = impl Copy;
struct Wrapped<T>(T);
impl Wrapped<Token> {
    fn code(&self) -> u8 { 43 }
}

fn a_check(value: Token) -> u8 { Wrapped(value).code() }

#[define_opaque(Token)]
fn z_token() -> Token { 47u16 }

fn main() {
    assert_eq!(a_check(z_token()), 43);
}
