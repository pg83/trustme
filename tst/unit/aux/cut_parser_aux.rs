// A model of winnow's parser combinators, used from another crate: `cut_err`
// and `separated_pair` return closures of this crate behind `impl Parser`.
pub trait Stream { fn next_char(&mut self) -> Option<char>; }
pub trait StreamIsPartial {}
pub trait Compare<T> {}
#[derive(Debug)]
pub struct Partial<I> { pub input: I }
impl<'a> Stream for Partial<&'a str> {
    fn next_char(&mut self) -> Option<char> { let c = self.input.chars().next()?; self.input = &self.input[c.len_utf8()..]; Some(c) }
}
impl<I> StreamIsPartial for Partial<I> {}
impl<'a> Compare<char> for Partial<&'a str> {}
pub trait ParserError<I>: Sized { fn from_input(input: &I) -> Self; }
pub trait ModalError { fn cut(self) -> Self; }
#[derive(Debug)]
pub enum ErrMode<E> { Backtrack(E), Cut(E) }
impl<I, E: ParserError<I>> ParserError<I> for ErrMode<E> { fn from_input(input: &I) -> Self { ErrMode::Backtrack(E::from_input(input)) } }
impl<E> ModalError for ErrMode<E> { fn cut(self) -> Self { match self { ErrMode::Backtrack(e) | ErrMode::Cut(e) => ErrMode::Cut(e) } } }
#[derive(Debug)]
pub struct ContextError;
impl<I> ParserError<I> for ContextError { fn from_input(_: &I) -> Self { ContextError } }
pub type ModalResult<O, E> = Result<O, ErrMode<E>>;

pub trait Parser<I, O, E> {
    fn parse_next(&mut self, input: &mut I) -> Result<O, E>;
}
impl<I, O, E, F> Parser<I, O, E> for F where F: FnMut(&mut I) -> Result<O, E>, I: Stream {
    fn parse_next(&mut self, i: &mut I) -> Result<O, E> { self(i) }
}
impl<I, E> Parser<I, char, E> for char where I: StreamIsPartial, I: Stream, I: Compare<char>, E: ParserError<I> {
    fn parse_next(&mut self, i: &mut I) -> Result<char, E> {
        match i.next_char() { Some(c) if c == *self => Ok(c), _ => Err(E::from_input(i)) }
    }
}
impl<I: Stream, O1, O2, O3, E: ParserError<I>, P1, P2, P3> Parser<I, (O1, O2, O3), E> for (P1, P2, P3)
where P1: Parser<I, O1, E>, P2: Parser<I, O2, E>, P3: Parser<I, O3, E> {
    fn parse_next(&mut self, i: &mut I) -> Result<(O1, O2, O3), E> {
        let a = self.0.parse_next(i)?; let b = self.1.parse_next(i)?; let c = self.2.parse_next(i)?; Ok((a, b, c))
    }
}
pub fn trace<I: Stream, O, E: ParserError<I>>(_name: &str, parser: impl Parser<I, O, E>) -> impl Parser<I, O, E> { parser }
pub fn cut_err<Input, Output, Error, ParseNext>(mut parser: ParseNext) -> impl Parser<Input, Output, Error>
where Input: Stream, Error: ParserError<Input> + ModalError, ParseNext: Parser<Input, Output, Error> {
    trace("cut_err", move |input: &mut Input| parser.parse_next(input).map_err(|e| e.cut()))
}
pub fn separated_pair<Input, O1, Sep, O2, Error, P1, SepParser, P2>(mut first: P1, mut sep: SepParser, mut second: P2) -> impl Parser<Input, (O1, O2), Error>
where Input: Stream, Error: ParserError<Input>, P1: Parser<Input, O1, Error>, SepParser: Parser<Input, Sep, Error>, P2: Parser<Input, O2, Error> {
    trace("separated_pair", move |input: &mut Input| {
        let o1 = first.parse_next(input)?;
        let _ = sep.parse_next(input)?;
        second.parse_next(input).map(|o2| (o1, o2))
    })
}
