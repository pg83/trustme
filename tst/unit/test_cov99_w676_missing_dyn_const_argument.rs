trait Input<const N: usize> {}
//@ compile-fail: Input
impl Input<23> for u8 {}

trait TupleValue {
    fn value(&self) -> usize;
}

impl<const N: usize> TupleValue for (&dyn Input<N>,) {
    fn value(&self) -> usize {
        N
    }
}

fn main() {
    let input: &dyn Input<23> = &7u8;
    assert_eq!(<(&dyn Input,)>::value(&(input,)), 23);
}
