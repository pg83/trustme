use std::marker::PhantomData;

trait Source {
    type Item;
    fn read(&self) -> Self::Item;
}

struct DefaultSource<T>(PhantomData<T>);

impl<T: Default> Source for DefaultSource<T> {
    type Item = T;
    fn read(&self) -> T { T::default() }
}

fn main() {
    let source = DefaultSource(PhantomData);
    let object: &dyn Source<Item = _> = &source;
    let result: u64 = object.read();
    let expected = 0u64;
    assert_eq!(result, expected);
}
