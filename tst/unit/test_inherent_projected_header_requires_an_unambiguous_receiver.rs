//@ compile-fail: error
trait Family { type Item; }
#[derive(Default)]
struct First;
impl Family for First { type Item = u8; }
#[derive(Default)]
struct Second;
impl Family for Second { type Item = u16; }
struct Holder<T, U>(T, U);
impl<T: Family> Holder<T, T::Item> {
    fn measure(&self) -> usize { std::mem::size_of::<T::Item>() }
}
fn main() {
    let value = Holder(Default::default(), Default::default());
    let _ = value.measure();
}
