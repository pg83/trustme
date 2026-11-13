use std::future::Future;

trait Finish {
    type Output;
    fn finish(self) -> Self::Output;
}

impl<F: Future> Finish for (F, F::Output, F::Output) {
    type Output = F::Output;
    fn finish(self) -> F::Output { self.1 }
}

fn finish<T: Finish>(value: T) -> T::Output {
    value.finish()
}

fn main() {
    let value = Default::default();
    let future = async move { value };
    let result: u8 = finish((future, value, value));
    assert_eq!(result, 0u8);
}
