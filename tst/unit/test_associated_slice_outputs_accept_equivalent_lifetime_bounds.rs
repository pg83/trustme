trait Produce { type Output; fn produce(self) -> Self::Output; }
struct Source<'a>(&'a [&'a u8]);
impl<'a> Produce for Source<'a> { type Output = &'a [&'a u8]; fn produce(self) -> Self::Output { self.0 } }
fn require<S, O>(source: S) -> O where S: Produce<Output = O> { source.produce() }
fn relay<'a, 'b>(source: Source<'a>) -> &'b [&'b u8] where 'a: 'b, 'b: 'a { require::<Source<'a>, &'b [&'b u8]>(source) }
fn main() { let n = 71u8; let values = [&n, &n]; let result = relay(Source(&values)); assert_eq!(result.len(), 2); assert_eq!(*result[1], 71); }
