use std::ops::Deref;

struct Chain<T, U>(Option<T>, U);

impl<T, U> Deref for Chain<T, U> {
    type Target = U;
    fn deref(&self) -> &U { &self.1 }
}

fn first<I: Iterator>(mut iterator: I) -> I::Item {
    iterator.next().unwrap()
}

fn chain<T, I: Iterator>(value: T, _: I) -> Chain<T, Chain<I::Item, ()>> {
    Chain(Some(value), Chain(None, ()))
}

fn main() {
    let mut iterator = [()].into_iter().map(|_| Default::default());
    let value = first(&mut iterator);
    let outer = chain(value, iterator);
    let selected = if std::hint::black_box(true) { &outer } else { &outer.1 };
    let value: Option<u8> = selected.0;
    assert!(value.is_none());
}
