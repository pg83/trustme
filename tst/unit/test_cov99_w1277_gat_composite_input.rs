trait Family {
    type Item<T>;
}

struct Identity;

impl Family for Identity {
    type Item<T> = T;
}

fn choose<F: Family, T>(_: T, item: F::Item<T>) -> F::Item<T> {
    item
}

fn main() {
    let item = choose::<Identity, _>(Some(1), Some(2));
    assert_eq!(item, Some(2));
}
