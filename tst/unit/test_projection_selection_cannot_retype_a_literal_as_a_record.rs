//@ compile-fail: error
trait Select<Input> { type Output; fn extract(self) -> Self::Output; }
struct Carrier<T>(T);
struct Record(u8);
impl<T> Select<Record> for Carrier<T> {
    type Output = T;
    fn extract(self) -> T { self.0 }
}
fn extract<I, T>(value: Carrier<T>) -> I where Carrier<T>: Select<I, Output=I> {
    <Carrier<T> as Select<I>>::extract(value)
}
fn main() {
    let _ = extract(Carrier(23));
}
