// `impl<O, E, A: Parser<Output = O, Error = E>, B: Parser<Output = O, Error = E>>
// Parser for Choice<(A, B)>`: `E` is fixed only through the where-clauses, so
// selecting the impl for `Choice<(ch::<?e1>(..), ch::<?e2>(..))>` relates both
// `Error`s through it, and the goal's `?e1` must come out bound to what `?e2`
// is. nom's `alt((char('+'), char('-')))` in `sign` and `recognize_float`.
use std::marker::PhantomData;
pub trait ParseError: Sized { fn make() -> Self; }
impl ParseError for () { fn make() -> Self {} }
pub trait Parser {
    type Output;
    type Error: ParseError;
    fn parse(&mut self) -> Result<Self::Output, Self::Error>;
}
pub struct Ch<E>(char, PhantomData<E>);
impl<E: ParseError> Parser for Ch<E> {
    type Output = char;
    type Error = E;
    fn parse(&mut self) -> Result<char, E> { if self.0 == '-' { Ok('-') } else { Err(E::make()) } }
}
pub fn ch<E: ParseError>(c: char) -> impl Parser<Output = char, Error = E> { Ch(c, PhantomData) }
pub struct Choice<T> { parser: T }
pub fn alt<List>(l: List) -> Choice<List> { Choice { parser: l } }
impl<O, E: ParseError, A: Parser<Output = O, Error = E>, B: Parser<Output = O, Error = E>> Parser for Choice<(A, B)> {
    type Output = O;
    type Error = E;
    fn parse(&mut self) -> Result<O, E> {
        match self.parser.0.parse() { Err(_) => self.parser.1.parse(), r => r }
    }
}
pub fn sign<E: ParseError>() -> Result<char, E> {
    alt((ch('+'), ch('-'))).parse()
}
fn main() {
    let r: Result<char, ()> = sign();
    assert_eq!(r.unwrap(), '-');
}
