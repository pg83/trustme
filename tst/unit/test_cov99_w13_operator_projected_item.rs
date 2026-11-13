use std::ops::Add;

struct First<I>(I);
impl<I: Iterator> Add<()> for First<I> {
    type Output = I::Item;
    fn add(mut self, _: ()) -> Self::Output {
        self.0.next().unwrap()
    }
}

fn main() {
    let value: u8 = First(
        [9u8].into_iter()
            .filter_map(|value| value.into())
            .filter_map(|value| value.into()),
    ) + ();
    assert_eq!(value, 9);
}
