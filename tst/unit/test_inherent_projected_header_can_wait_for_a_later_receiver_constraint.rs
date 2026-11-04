trait Family { type Item; }
#[derive(Default)]
struct First;
impl Family for First { type Item = u8; }
#[derive(Default)]
struct Second;
impl Family for Second { type Item = u16; }
struct Holder<T, U>(T, U);
impl<T: Family> Holder<T, T::Item> {
    fn measure(&self) -> usize { std::mem::size_of::<T::Item>() + 787 }
}
fn main() {
    let value = Holder(Default::default(), Default::default());
    let result = value.measure();
    let _: &Holder<First, u8> = &value;
    assert_eq!(result, 788);
    let value = Holder(Default::default(), Default::default());
    let result = value.measure();
    let _: &Holder<Second, u16> = &value;
    assert_eq!(result, 789);
}
