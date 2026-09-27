// nom-language's `precedence(unary_op(2, map(tag("-"), |_| ..)), ..,
// map_res(digit1, |s: &str| ..), |op: Operation<..>| ..)` did not converge
// ("Typecheck ran for too many iterations"; this reduction stops at "type
// annotations needed" instead). rustc checks a call's arguments in order:
// `map`'s closure argument is checked and bound to its parameter before
// `map(..)` itself is coerced into `unary_op`'s parameter; only what is inside
// a closure's body comes after the signature its expectation gives it. Our
// exemption for bindings inside a closure covered the closure's own binding
// too (its place is the closure's start), so the enclosing argument
// coercions ran ahead of it.
trait ParseError<I> {
    fn from_input(input: I) -> Self;
}
trait FromExternalError<I, E> {
    fn from_external_error(input: I, e: E) -> Self;
}
#[derive(Debug, PartialEq)]
struct Error<I>(I);
impl<I> ParseError<I> for Error<I> {
    fn from_input(input: I) -> Self { Error(input) }
}
impl<I, E> FromExternalError<I, E> for Error<I> {
    fn from_external_error(input: I, _e: E) -> Self { Error(input) }
}
trait Parser<I> {
    type Output;
    type Error: ParseError<I>;
    fn parse(&mut self, input: I) -> Result<(I, Self::Output), Self::Error>;
}
impl<I, O, E: ParseError<I>, F: FnMut(I) -> Result<(I, O), E>> Parser<I> for F {
    type Output = O;
    type Error = E;
    fn parse(&mut self, input: I) -> Result<(I, O), E> { self(input) }
}
struct Map<F, G>(F, G);
impl<I, O2, E: ParseError<I>, F: Parser<I, Error = E>, G: FnMut(<F as Parser<I>>::Output) -> O2> Parser<I> for Map<F, G> {
    type Output = O2;
    type Error = E;
    fn parse(&mut self, input: I) -> Result<(I, O2), E> {
        let (rest, value) = self.0.parse(input)?;
        Ok((rest, (self.1)(value)))
    }
}
fn map<I, O, E, F, G>(parser: F, f: G) -> Map<F, G>
where F: Parser<I, Error = E>, G: FnMut(<F as Parser<I>>::Output) -> O {
    Map(parser, f)
}
struct MapRes<F, G>(F, G);
impl<I: Clone, O2, E: ParseError<I> + FromExternalError<I, E2>, E2, F: Parser<I, Error = E>, G: FnMut(<F as Parser<I>>::Output) -> Result<O2, E2>> Parser<I> for MapRes<F, G> {
    type Output = O2;
    type Error = E;
    fn parse(&mut self, input: I) -> Result<(I, O2), E> {
        let (rest, value) = self.0.parse(input.clone())?;
        match (self.1)(value) {
            Ok(v) => Ok((rest, v)),
            Err(e) => Err(E::from_external_error(input, e)),
        }
    }
}
fn map_res<I: Clone, O, E: ParseError<I> + FromExternalError<I, E2>, E2, F, G>(parser: F, f: G) -> MapRes<F, G>
where F: Parser<I, Error = E>, G: FnMut(<F as Parser<I>>::Output) -> Result<O, E2> {
    MapRes(parser, f)
}
fn tag<'a, E: ParseError<&'a str>>(word: &'static str) -> impl Fn(&'a str) -> Result<(&'a str, &'a str), E> {
    move |input: &'a str| match input.strip_prefix(word) {
        Some(rest) => Ok((rest, &input[..word.len()])),
        None => Err(E::from_input(input)),
    }
}
fn digit1<'a, E: ParseError<&'a str>>(input: &'a str) -> Result<(&'a str, &'a str), E> {
    let n = input.chars().take_while(|c| c.is_ascii_digit()).count();
    if n == 0 { Err(E::from_input(input)) } else { Ok((&input[n..], &input[..n])) }
}
pub struct Unary<V, Q> { value: V, precedence: Q }
pub struct Binary<V, Q> { value: V, precedence: Q, assoc: Assoc }
#[derive(Clone, Copy)]
pub enum Assoc { Left, Right }
pub enum Operation<P1, P2, P3, O> { Prefix(P1, O), Postfix(O, P2), Binary(O, P3, O) }
fn unary_op<I, O, E, P, Q>(precedence: Q, mut parser: P) -> impl FnMut(I) -> Result<(I, Unary<O, Q>), E>
where P: Parser<I, Output = O, Error = E>, Q: Ord + Copy {
    move |input| match parser.parse(input) { Ok((i, value)) => Ok((i, Unary { value, precedence })), Err(e) => Err(e) }
}
fn binary_op<I, O, E, P, Q>(precedence: Q, assoc: Assoc, mut parser: P) -> impl FnMut(I) -> Result<(I, Binary<O, Q>), E>
where P: Parser<I, Output = O, Error = E>, Q: Ord + Copy {
    move |input| match parser.parse(input) { Ok((i, value)) => Ok((i, Binary { value, precedence, assoc })), Err(e) => Err(e) }
}
fn precedence<I, O, E, E2, F, G, H1, H3, H2, P1, P2, P3, Q>(mut prefix: H1, mut postfix: H2, mut binary: H3, mut operand: F, mut fold: G) -> impl FnMut(I) -> Result<(I, O), E>
where
    I: Clone + PartialEq,
    E: ParseError<I> + FromExternalError<I, E2>,
    F: Parser<I, Output = O, Error = E>,
    G: FnMut(Operation<P1, P2, P3, O>) -> Result<O, E2>,
    H1: Parser<I, Output = Unary<P1, Q>, Error = E>,
    H2: Parser<I, Output = Unary<P2, Q>, Error = E>,
    H3: Parser<I, Output = Binary<P3, Q>, Error = E>,
    Q: Ord + Copy,
{
    move |input: I| {
        let (rest, value) = match prefix.parse(input.clone()) {
            Ok((rest, op)) => {
                let (rest, value) = operand.parse(rest)?;
                let _ = op.precedence;
                match fold(Operation::Prefix(op.value, value)) { Ok(v) => (rest, v), Err(e) => return Err(E::from_external_error(input, e)) }
            }
            Err(_) => operand.parse(input.clone())?,
        };
        let (rest, value) = match postfix.parse(rest.clone()) {
            Ok((rest2, op)) => match fold(Operation::Postfix(value, op.value)) { Ok(v) => (rest2, v), Err(e) => return Err(E::from_external_error(input, e)) },
            Err(_) => (rest, value),
        };
        match binary.parse(rest.clone()) {
            Ok((rest2, op)) => {
                let _ = (op.precedence, op.assoc);
                let (rest3, rhs) = operand.parse(rest2)?;
                match fold(Operation::Binary(value, op.value, rhs)) { Ok(v) => Ok((rest3, v)), Err(e) => Err(E::from_external_error(input, e)) }
            }
            Err(_) => Ok((rest, value)),
        }
    }
}
pub enum Expr { Num(i64), Neg(Box<Expr>), Add(Box<Expr>, Box<Expr>) }
enum PrefixOp { Negate }
enum PostfixOp { Bang }
enum BinaryOp { Addition }
fn expression(i: &str) -> Result<(&str, Expr), Error<&str>> {
  precedence(
    unary_op(2, map(tag("-"), |_| PrefixOp::Negate)),
    unary_op(1, map(tag("!"), |_| PostfixOp::Bang)),
    binary_op(4, Assoc::Left, map(tag("+"), |_| BinaryOp::Addition)),
    map_res(digit1, |s: &str| Ok::<Expr, std::num::ParseIntError>(Expr::Num(s.len() as i64))),
    |op: Operation<PrefixOp, PostfixOp, BinaryOp, Expr>| -> Result<Expr, ()> {
      use Operation::*;
      match op {
        Prefix(PrefixOp::Negate, e) => Ok(Expr::Neg(Box::new(e))),
        Postfix(e, PostfixOp::Bang) => Ok(e),
        Binary(lhs, BinaryOp::Addition, rhs) => Ok(Expr::Add(Box::new(lhs), Box::new(rhs))),
      }
    },
  )(i)
}
fn main() {
    assert!(matches!(expression("1+22"), Ok(("", Expr::Add(..)))));
    assert!(matches!(expression("-1"), Ok(("", Expr::Neg(..)))));
}
