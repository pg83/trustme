//@ edition: 2021

struct Selected;

trait SelectIntoIter {
    fn into_iter(self) -> Selected;
}

impl<T> SelectIntoIter for Box<[T]> {
    fn into_iter(self) -> Selected {
        Selected
    }
}

fn expect_selected(_: Selected) {}

fn main() {
    let values: Box<[_]> = vec![1].into_boxed_slice();
    expect_selected(values.into_iter());
}
