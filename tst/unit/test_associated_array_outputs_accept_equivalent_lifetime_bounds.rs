trait Produce { type Output; fn produce(self) -> Self::Output; }
struct Source<'a>(&'a u8);
impl<'a> Produce for Source<'a> { type Output = [&'a u8; 2]; fn produce(self) -> Self::Output { [self.0; 2] } }
fn require<S, O>(source: S) -> O where S: Produce<Output = O> { source.produce() }
fn relay<'a, 'b>(source: Source<'a>) -> [&'b u8; 2] where 'a: 'b, 'b: 'a { require::<Source<'a>, [&'b u8; 2]>(source) }
fn main() { let n = 59u8; let result = relay(Source(&n)); assert_eq!(result, [&n, &n]); assert!(std::ptr::eq(result[1], &n)); }
