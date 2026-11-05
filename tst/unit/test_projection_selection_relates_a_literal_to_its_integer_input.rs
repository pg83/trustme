//@ compile-variant: -O
trait Select<Input> { type Output; fn extract(self) -> Self::Output; }
struct Carrier<T>(T);
impl<T> Select<u8> for Carrier<T> {
    type Output = T;
    fn extract(self) -> T { self.0 }
}
fn extract<I, T>(value: Carrier<T>) -> I where Carrier<T>: Select<I, Output=I> {
    <Carrier<T> as Select<I>>::extract(value)
}
fn main() {
    let value = extract(Carrier(23));
    assert_eq!(value, 23u8);
}
