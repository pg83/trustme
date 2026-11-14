trait Input<const N: usize = 7> {}

impl Input<7> for () {}

trait Extension {
    fn number(&self) -> usize;
}

impl<const N: usize> Extension for dyn Input<N> {
    fn number(&self) -> usize {
        N
    }
}

fn main() {
    let value: &dyn Input<7> = &();
    assert_eq!(<dyn Input>::number(value), 7);
}
