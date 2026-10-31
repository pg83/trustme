trait Ready { fn seed() -> Self; }
impl Ready for u16 { fn seed() -> Self { 12 } }
trait Mark<T> { fn read(self) -> T; }
impl<T> Mark<T> for (T,) { fn read(self) -> T { self.0 } }
trait Family {
    type Item<T>: Mark<T> where T: Ready;
    fn make<T: Ready>() -> Self::Item<T>;
}
struct Tuples;
impl Family for Tuples {
    type Item<T> = (T,) where T: Ready;
    fn make<T: Ready>() -> Self::Item<T> { (T::seed(),) }
}
fn measured<T: Mark<u16>>(value: T) -> u16 { value.read() }
fn run<F: Family>() -> u16 { measured(F::make()) }
fn main() { assert_eq!(run::<Tuples>(), 12); }
