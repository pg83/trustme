// winnow's issues.rs: `fill_pair.parse_peek(b"123,456,")` with
// `impl<I, O, E, F: FnMut(&mut I) -> Result<O, E>> Parser<I, O, E> for F`.
// The impl's implicit `I: Sized` was checked before its `F: FnMut(&mut I)`
// bound had bound `I` from the fn item, answered "ambiguous", and left the
// candidate ambiguous for good; the method's trait parameters were then not
// decided before its argument, the argument `&[u8; 3]` fixed `I`, and no
// method applied. rustc evaluates the implicit `Sized` obligation with the
// impl's other nested obligations, so its order among them does not matter.
trait Parser<I, O, E> {
    fn parse_next(&mut self, input: &mut I) -> Result<O, E>;
    fn parse_peek(&mut self, mut input: I) -> Result<(I, O), E> {
        let o = self.parse_next(&mut input)?;
        Ok((input, o))
    }
}
impl<I, O, E, F: FnMut(&mut I) -> Result<O, E>> Parser<I, O, E> for F {
    fn parse_next(&mut self, input: &mut I) -> Result<O, E> {
        self(input)
    }
}
fn fill_pair<'i>(input: &mut &'i [u8]) -> Result<&'i [u8], ()> {
    let head = &input[..1];
    *input = &input[1..];
    Ok(head)
}
fn main() {
    let (rest, head) = fill_pair.parse_peek(b"123").unwrap();
    assert_eq!((rest, head), (&b"23"[..], &b"1"[..]));
}
