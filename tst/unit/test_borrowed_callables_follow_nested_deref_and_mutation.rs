//@ compile-variant: -O
use std::ops::{Deref, DerefMut};
struct Layer<F>(F);
impl<F> Deref for Layer<F> {
    type Target = F;
    fn deref(&self) -> &F { &self.0 }
}
impl<F> DerefMut for Layer<F> {
    fn deref_mut(&mut self) -> &mut F { &mut self.0 }
}
fn main() {
    let bias = std::hint::black_box(311i32);
    let callable = Layer(Layer(|value: i32| value + bias));
    let reference = &&callable;
    assert_eq!(reference(7), 318);
    let mut total = 0;
    {
        let mut stateful = Layer(Layer(|value: i32| { total += value; total }));
        let mut first = &mut stateful;
        let second = &mut first;
        assert_eq!(second(13), 13);
        assert_eq!(second(-5), 8);
    }
    assert_eq!(total, 8);
}
