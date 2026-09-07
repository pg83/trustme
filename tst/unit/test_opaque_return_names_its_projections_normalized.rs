/* winnow 1.0's `one_of`: `trace("one_of", any.verify(move |t: &<I as Stream>::Token| ..))` returned
   as `impl Parser<I, <I as Stream>::Token, E>` - the body's opaque type names the projection through
   `trace`'s captured arguments, and the declared return names the same rigid projection; the
   final check must see one type, not two spellings of the projection. */
use std::borrow::Borrow;
use std::marker::PhantomData;

trait Stream {
    type Token: Clone;
    fn next_token(&mut self) -> Self::Token;
}

trait ContainsToken<T> {
    fn contains_token(&self, t: T) -> bool;
}

trait Parser<I, O> {
    fn parse(&mut self, i: &mut I) -> O;
    fn verify<G, O2: ?Sized>(self, f: G) -> Verify<Self, G, O2>
    where
        Self: Sized,
        G: FnMut(&O2) -> bool,
        O: Borrow<O2>,
    {
        Verify { p: self, f, _o: PhantomData }
    }
}

struct Verify<P, G, O2: ?Sized> {
    p: P,
    f: G,
    _o: PhantomData<O2>,
}

impl<I, O, O2: ?Sized, P: Parser<I, O>, G: FnMut(&O2) -> bool> Parser<I, O> for Verify<P, G, O2>
where
    O: Borrow<O2>,
{
    fn parse(&mut self, i: &mut I) -> O {
        let o = self.p.parse(i);
        assert!((self.f)(o.borrow()));
        o
    }
}

impl<I: Stream, F: FnMut(&mut I) -> I::Token> Parser<I, I::Token> for F {
    fn parse(&mut self, i: &mut I) -> I::Token {
        self(i)
    }
}

fn any<I: Stream>(i: &mut I) -> I::Token {
    i.next_token()
}

fn trace<I, O, P: Parser<I, O>>(_name: &str, p: P) -> impl Parser<I, O> {
    p
}

fn one_of<I, S>(set: S) -> impl Parser<I, <I as Stream>::Token>
where
    I: Stream,
    <I as Stream>::Token: Clone,
    S: ContainsToken<<I as Stream>::Token>,
{
    trace("one_of", any.verify(move |t: &<I as Stream>::Token| set.contains_token(t.clone())))
}

struct Chars(Vec<char>);
impl Stream for Chars {
    type Token = char;
    fn next_token(&mut self) -> char {
        self.0.remove(0)
    }
}
impl ContainsToken<char> for [char; 2] {
    fn contains_token(&self, t: char) -> bool {
        self.contains(&t)
    }
}

fn main() {
    let mut input = Chars(vec!['a', 'b']);
    let mut p = one_of(['a', 'b']);
    assert_eq!(p.parse(&mut input), 'a');
}
