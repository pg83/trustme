struct Adapter<I>(I);
struct List<T>(T);

trait ExtendOne<I> {}

impl<I: IntoIterator> ExtendOne<I> for List<I::Item> {}

impl<I: Iterator> Iterator for Adapter<I> {
    type Item = I::Item;

    fn next(&mut self) -> Option<I::Item> {
        self.0.next()
    }
}

fn main() {
    let mut adapter = Adapter([7, 9].into_iter());
    assert_eq!(adapter.next(), Some(7));
    assert_eq!(adapter.next(), Some(9));
    assert_eq!(adapter.next(), None);
}
