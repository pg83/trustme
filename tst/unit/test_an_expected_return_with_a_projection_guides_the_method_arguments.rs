// `P.parse(b"012346")` against an expected `IResult<&[u8], ..>`: upstream
// relates the method's return to the expectation first and keeps what it
// learns unless the relation is an error - the `Self::Output` left over is
// an ambiguous obligation, no error - so `I` is `&[u8]` and the argument
// unsizes into it. nom's tests/issues.rs `length_data(be_u16).parse(..)`.
pub type IResult<I, O, E> = Result<(I, O), E>;
pub trait Parser<I> {
    type Output;
    fn parse(&mut self, input: I) -> IResult<I, Self::Output, ()>;
}
impl<I, O, F> Parser<I> for F where F: FnMut(I) -> IResult<I, O, ()> {
    type Output = O;
    fn parse(&mut self, i: I) -> IResult<I, O, ()> { self(i) }
}
fn id<I>(i: I) -> IResult<I, u16, ()> { Ok((i, 2)) }
struct P;
impl<I> Parser<I> for P {
    type Output = u16;
    fn parse(&mut self, i: I) -> IResult<I, u16, ()> { Ok((i, 2)) }
}
fn main() {
    let r: IResult<&[u8], u16, ()> = P.parse(b"012346");
    assert!(r.is_ok());
    let r: IResult<&[u8], u16, ()> = id.parse(b"012346");
    assert!(r.is_ok());
}
