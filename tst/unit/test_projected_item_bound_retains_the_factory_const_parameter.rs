//@ compile-variant: -O
trait Scale<const N: usize> { fn scale(self) -> u16; }
trait Factory<const N: usize> { type Output: Scale<N>; fn make() -> Self::Output; }
struct Value(u16);
impl<const N: usize> Scale<N> for Value {
    fn scale(self) -> u16 { self.0 * N as u16 }
}
struct Source;
impl<const N: usize> Factory<N> for Source {
    type Output = Value;
    fn make() -> Value { Value(9) }
}
fn through<const N: usize, F: Factory<N>>() -> u16 {
    <F::Output as Scale<N>>::scale(F::make())
}
fn main() {
    assert_eq!(through::<3, Source>(), 27);
    assert_eq!(through::<7, Source>(), 63);
}
