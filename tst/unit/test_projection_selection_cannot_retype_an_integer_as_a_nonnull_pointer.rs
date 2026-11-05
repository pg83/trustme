//@ compile-fail: error
#![feature(pattern_types, pattern_type_macro)]
type Pointer = std::pat::pattern_type!(*const u8 is !null);
trait Select<Input> { type Output; fn extract(self) -> Self::Output; }
struct Carrier<T>(T);
impl<T> Select<Pointer> for Carrier<T> {
    type Output = T;
    fn extract(self) -> T { self.0 }
}
fn extract<I, T>(value: Carrier<T>) -> I where Carrier<T>: Select<I, Output=I> {
    <Carrier<T> as Select<I>>::extract(value)
}
fn main() {
    let _ = extract(Carrier(23));
}
