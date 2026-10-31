trait Produce { type Output; fn produce(self) -> Self::Output; }
struct Source<'a>(&'a &'a u8);
impl<'a> Produce for Source<'a> { type Output = *const &'a u8; fn produce(self) -> Self::Output { self.0 as *const _ } }
fn require<S, O>(source: S) -> O where S: Produce<Output = O> { source.produce() }
fn relay<'a, 'b>(source: Source<'a>) -> *const &'b u8 where 'a: 'b, 'b: 'a { require::<Source<'a>, *const &'b u8>(source) }
fn main() { let n = 73u8; let reference = &n; let result = relay(Source(&reference)); assert!(std::ptr::eq(result, &reference)); assert_eq!(unsafe { **result }, 73); }
