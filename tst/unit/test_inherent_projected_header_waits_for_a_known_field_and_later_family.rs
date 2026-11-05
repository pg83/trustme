//@ compile-variant: -O

trait Family { type Item; }
#[derive(Default)]
struct Source;
impl Family for Source { type Item = u16; }
struct Holder<T, U>(T, U);

impl<T: Family> Holder<T, T::Item> {
    fn read(&self) -> &T::Item { &self.1 }
}

fn main() {
    let value = Holder(Default::default(), 47u16);
    let result = *value.read();
    let _: Holder<Source, u16> = value;
    assert_eq!(result, 47);
}
