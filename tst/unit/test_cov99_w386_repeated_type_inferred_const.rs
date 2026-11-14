struct Cell<const N: usize>;

trait Same {
    fn same(self) -> usize;
}

impl<T> Same for (T, T) {
    fn same(self) -> usize {
        17
    }
}

fn main() {
    assert_eq!((Cell::<_>, Cell::<3>).same(), 17);
}
