use std::marker::PhantomData;

trait Enabled {}
impl Enabled for u32 {}

trait Shape {
    type Output;
}

struct Holder<T>(PhantomData<T>);

impl<T: Enabled> Shape for Holder<T> {
    type Output = u16;
}

fn exchange<T>(value: <Holder<T> as Shape>::Output, _: Holder<T>) -> <Holder<T> as Shape>::Output
where
    Holder<T>: Shape,
{
    value
}

fn main() {
    let result = exchange(83u16, Holder::<u32>(PhantomData));
    assert_eq!(result, 83);
}
