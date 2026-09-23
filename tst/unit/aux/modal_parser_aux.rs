pub trait Stream { type Slice; fn take(&mut self) -> Self::Slice; }
pub trait Compare<T> { fn cmp(&self, t: T) -> bool; }
pub trait ParserError<I> { fn new() -> Self; }
pub struct Raw<'a>(pub &'a [u8]);
impl<'a> Stream for Raw<'a> { type Slice = &'a [u8]; fn take(&mut self) -> &'a [u8] { self.0 } }
impl<'a, 's, const N: usize> Compare<&'s [u8; N]> for Raw<'a> { fn cmp(&self, t: &'s [u8; N]) -> bool { self.0.starts_with(t) } }
pub struct Wrap<I, S>(pub I, pub S);
impl<I: Stream, S> Stream for Wrap<I, S> { type Slice = <I as Stream>::Slice; fn take(&mut self) -> Self::Slice { self.0.take() } }
impl<T, I: Compare<T>, S> Compare<T> for Wrap<I, S> { fn cmp(&self, t: T) -> bool { self.0.cmp(t) } }
pub type In<'a> = Wrap<Wrap<Raw<'a>, ()>, ()>;
pub struct Err<E>(E);
impl<I, E: ParserError<I>> ParserError<I> for Err<E> { fn new() -> Self { Err(E::new()) } }
pub struct Ctx;
impl<I> ParserError<I> for Ctx { fn new() -> Self { Ctx } }
pub trait Parser<I, O, E> {
    fn parse_next(&mut self, i: &mut I) -> Result<O, E>;
    fn void(self) -> Void<Self, I, O, E> where Self: Sized { Void(self, std::marker::PhantomData) }
}
pub struct Void<P, I, O, E>(P, std::marker::PhantomData<(I, O, E)>);
impl<I, O, E, P: Parser<I, O, E>> Parser<I, (), E> for Void<P, I, O, E> {
    fn parse_next(&mut self, i: &mut I) -> Result<(), E> { self.0.parse_next(i).map(|_| ()) }
}
impl<'s, I, E: ParserError<I>, const N: usize> Parser<I, <I as Stream>::Slice, E> for &'s [u8; N]
where I: Compare<&'s [u8; N]>, I: Stream {
    fn parse_next(&mut self, i: &mut I) -> Result<I::Slice, E> { let _ = i.cmp(*self); Ok(i.take()) }
}
impl<I, O, E, F: FnMut(&mut I) -> Result<O, E>> Parser<I, O, E> for F {
    fn parse_next(&mut self, i: &mut I) -> Result<O, E> { self(i) }
}
impl<I: Stream, O1, O2, E: ParserError<I>, P1: Parser<I, O1, E>, P2: Parser<I, O2, E>> Parser<I, (O1, O2), E> for (P1, P2) {
    fn parse_next(&mut self, i: &mut I) -> Result<(O1, O2), E> { Ok((self.0.parse_next(i)?, self.1.parse_next(i)?)) }
}
pub fn opt<I: Stream, O, E: ParserError<I>, P: Parser<I, O, E>>(mut p: P) -> impl Parser<I, Option<O>, E> {
    move |i: &mut I| p.parse_next(i).map(Some)
}
pub trait Modal<I, O, E>: Parser<I, O, Err<E>> {}
impl<I, O, E, P: Parser<I, O, Err<E>>> Modal<I, O, E> for P {}
