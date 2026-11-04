trait Family { type Item; }
#[derive(Default)]
struct First;
impl Family for First { type Item = u8; }
#[derive(Default)]
struct Second;
impl Family for Second { type Item = u16; }
struct Holder<T, U>(T, U);
impl<T: Family> Holder<T, T::Item> {
    fn measure(&self) -> usize { std::mem::size_of::<T::Item>() + 797 }
}
fn main() {
    let create = || Holder(Default::default(), Default::default());
    let value = create();
    let result = value.measure();
    let enforce = || { let _: &Holder<Second, u16> = &value; };
    enforce();
    assert_eq!(result, 799);
}
