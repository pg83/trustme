trait Produce { type Output; fn produce(self) -> Self::Output; }
struct Holder<'a>(&'a u8);
struct Source<'a>(&'a u8);
impl<'a> Produce for Source<'a> { type Output = Holder<'a>; fn produce(self) -> Self::Output { Holder(self.0) } }
fn require<S, O>(source: S) -> O where S: Produce<Output = O> { source.produce() }
fn relay<'a, 'b>(source: Source<'a>) -> Holder<'b> where 'a: 'b, 'b: 'a { require::<Source<'a>, Holder<'b>>(source) }
fn main() { let n = 79u8; let result = relay(Source(&n)); assert_eq!(*result.0, 79); assert!(std::ptr::eq(result.0, &n)); }
