use std::marker::PhantomData;

struct Holder<T>(PhantomData<T>);

trait Source {
    type Output;
    fn get(&self) -> Self::Output;
}

impl<T> Source for Holder<T> {
    type Output = Option<T>;
    fn get(&self) -> Option<T> { None }
}

fn lift<S: Source>(source: &S) -> S::Output {
    source.get()
}

fn main() {
    let mut source = Holder(PhantomData);
    let output = lift(&source);
    source = Holder::<u16>(PhantomData);
    assert_eq!(output, None::<u16>);
    let _: &Holder<u16> = &source;
}
