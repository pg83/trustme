//@ run-pass
// combine 4.6.4 `string_cmp`: `tokens_cmp(s.chars(), cmp).map(move |_| s).expected(s)`,
// returned as `impl Parser<Input, Output = &'a str>`.  `.map` is looked up on
// `TokensCmp<C, Chars, ?I>` with `?I` still open, and the only `Parser` impl asks
// `C: FnMut(char, <?I as StreamOnce>::Token) -> bool`.  Upstream normalizes the
// projection over `?I` to a fresh variable and the where-clause
// `C: FnMut(char, char) -> bool` may still prove it; read as rigid, the projection
// mismatched `char`, the impl was dropped, and the lookup waited for `?I` while
// `?I` waited for the lookup.
use std::fmt;
use std::marker::PhantomData;

pub trait StreamError<Item, Range>: Sized {}

pub trait ParseError<Item, Range, Position>: Sized + PartialEq {
    type StreamError: StreamError<Item, Range>;
    fn empty(position: Position) -> Self;
}

pub trait StreamOnce {
    type Token: Clone;
    type Range: Clone;
    type Position: Clone + Ord;
    type Error: ParseError<Self::Token, Self::Range, Self::Position>;
    fn uncons(&mut self) -> Result<Self::Token, ()>;
}

pub trait Positioned: StreamOnce {
    fn position(&self) -> Self::Position;
}

pub trait ResetStream: StreamOnce {
    type Checkpoint: Clone;
    fn checkpoint(&self) -> Self::Checkpoint;
    fn reset(&mut self, checkpoint: Self::Checkpoint);
}

pub trait Stream: StreamOnce + ResetStream + Positioned {}

impl<Input> Stream for Input
where
    Input: StreamOnce + Positioned + ResetStream,
    Input::Error: ParseError<Input::Token, Input::Range, Input::Position>,
{
}

pub enum Info<T, R, F> {
    Token(T),
    Range(R),
    Static(F),
}

pub trait ErrorInfo<'s, T, R> {
    type Format: fmt::Display;
    fn into_info(&'s self) -> Info<T, R, Self::Format>;
}

impl<'s, 'a, T, R, F> ErrorInfo<'s, T, R> for &'a F
where
    F: ErrorInfo<'s, T, R>,
{
    type Format = F::Format;
    fn into_info(&'s self) -> Info<T, R, Self::Format> {
        (**self).into_info()
    }
}

impl<'s, T, R> ErrorInfo<'s, T, R> for &'static str {
    type Format = &'static str;
    fn into_info(&self) -> Info<T, R, Self::Format> {
        Info::Static(*self)
    }
}

pub trait Parser<Input: Stream> {
    type Output;
    fn parse(&mut self, input: &mut Input) -> Result<Self::Output, Input::Error>;

    fn map<F, B>(self, f: F) -> Map<Self, F>
    where
        Self: Sized,
        F: FnMut(Self::Output) -> B,
    {
        Map(self, f)
    }

    fn expected<S>(self, msg: S) -> Expected<Self, S>
    where
        Self: Sized,
        S: for<'s> ErrorInfo<'s, Input::Token, Input::Range>,
    {
        Expected(self, msg)
    }
}

pub struct Map<P, F>(P, F);

impl<Input, A, B, P, F> Parser<Input> for Map<P, F>
where
    Input: Stream,
    P: Parser<Input, Output = A>,
    F: FnMut(A) -> B,
{
    type Output = B;
    fn parse(&mut self, input: &mut Input) -> Result<B, Input::Error> {
        self.0.parse(input).map(&mut self.1)
    }
}

pub struct Expected<P, S>(P, S);

impl<Input, P, S> Parser<Input> for Expected<P, S>
where
    P: Parser<Input>,
    Input: Stream,
    S: for<'s> ErrorInfo<'s, Input::Token, Input::Range>,
{
    type Output = P::Output;
    fn parse(&mut self, input: &mut Input) -> Result<P::Output, Input::Error> {
        self.0.parse(input)
    }
}

pub struct TokensCmp<C, T, Input> {
    cmp: C,
    tokens: T,
    _marker: PhantomData<fn(Input) -> Input>,
}

impl<Input, C, T> Parser<Input> for TokensCmp<C, T, Input>
where
    C: FnMut(T::Item, Input::Token) -> bool,
    T: Clone + IntoIterator,
    Input: Stream,
{
    type Output = T;
    fn parse(&mut self, input: &mut Input) -> Result<T, Input::Error> {
        let start = input.position();
        for expected in self.tokens.clone() {
            match input.uncons() {
                Ok(tok) => {
                    if !(self.cmp)(expected, tok) {
                        return Err(Input::Error::empty(start));
                    }
                }
                Err(()) => return Err(Input::Error::empty(start)),
            }
        }
        Ok(self.tokens.clone())
    }
}

pub fn tokens_cmp<C, T, I>(tokens: T, cmp: C) -> TokensCmp<C, T, I>
where
    C: FnMut(T::Item, I::Token) -> bool,
    T: Clone + IntoIterator,
    I: Stream,
{
    TokensCmp { cmp, tokens, _marker: PhantomData }
}

pub fn string_cmp<'a, C, Input>(s: &'static str, cmp: C) -> impl Parser<Input, Output = &'a str>
where
    C: FnMut(char, char) -> bool,
    Input: Stream<Token = char>,
    Input::Error: ParseError<Input::Token, Input::Range, Input::Position>,
{
    tokens_cmp(s.chars(), cmp).map(move |_| s).expected(s)
}

#[derive(Clone, PartialEq, Debug)]
pub struct Errors(usize);

impl StreamError<char, &'static str> for () {}

impl ParseError<char, &'static str, usize> for Errors {
    type StreamError = ();
    fn empty(position: usize) -> Self {
        Errors(position)
    }
}

#[derive(Clone)]
pub struct Chars {
    s: &'static str,
    pos: usize,
}

impl StreamOnce for Chars {
    type Token = char;
    type Range = &'static str;
    type Position = usize;
    type Error = Errors;
    fn uncons(&mut self) -> Result<char, ()> {
        let c = self.s[self.pos..].chars().next().ok_or(())?;
        self.pos += c.len_utf8();
        Ok(c)
    }
}

impl Positioned for Chars {
    fn position(&self) -> usize {
        self.pos
    }
}

impl ResetStream for Chars {
    type Checkpoint = usize;
    fn checkpoint(&self) -> usize {
        self.pos
    }
    fn reset(&mut self, checkpoint: usize) {
        self.pos = checkpoint;
    }
}

fn main() {
    let mut input = Chars { s: "RusT!", pos: 0 };
    let mut parser = string_cmp("rust", |l: char, r: char| l.eq_ignore_ascii_case(&r));
    assert_eq!(parser.parse(&mut input), Ok("rust"));
    assert_eq!(input.position(), 4);
    assert_eq!(parser.parse(&mut input), Err(Errors(4)));
}
