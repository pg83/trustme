//@ compile-variant: -O
#![feature(pattern_types, pattern_type_macro)]
type Small = std::pat::pattern_type!(u8 is 1..=31);
trait Select<Input> { type Output; fn extract(self) -> Self::Output; }
struct Carrier<T>(T);
impl<T> Select<Small> for Carrier<T> {
    type Output = T;
    fn extract(self) -> T { self.0 }
}
fn extract<I, T>(value: Carrier<T>) -> I where Carrier<T>: Select<I, Output=I> {
    <Carrier<T> as Select<I>>::extract(value)
}
fn main() {
    let value = extract(Carrier(23));
    let raw: u8 = unsafe { std::mem::transmute(value) };
    assert_eq!(raw, 23);
}
