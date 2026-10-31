trait Produce { type Output; fn produce(self) -> Self::Output; }
struct Source<'a>(&'a u8, &'a u16);
impl<'a> Produce for Source<'a> { type Output = (&'a u8, &'a u16); fn produce(self) -> Self::Output { (self.0, self.1) } }
fn require<S, O>(source: S) -> O where S: Produce<Output = O> { source.produce() }
fn relay<'a, 'b>(source: Source<'a>) -> (&'b u8, &'b u16) where 'a: 'b, 'b: 'a { require::<Source<'a>, (&'b u8, &'b u16)>(source) }
fn main() { let a = 61u8; let b = 67u16; let result = relay(Source(&a, &b)); assert_eq!((*result.0, *result.1), (61, 67)); }
