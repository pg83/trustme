//@ edition: 2018

struct Selected;

trait SelectIntoIter {
    fn into_iter(self) -> Selected;
}

impl<T, const N: usize> SelectIntoIter for [T; N] {
    fn into_iter(self) -> Selected {
        Selected
    }
}

fn expect_selected(_: Selected) {}

fn main() {
    expect_selected([1].into_iter());
}
