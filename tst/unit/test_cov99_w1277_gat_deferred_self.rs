trait Family {
    type Item<T>;
}

struct Identity;
struct Wrapped;

impl Family for Identity {
    type Item<T> = T;
}

impl Family for Wrapped {
    type Item<T> = (T,);
}

fn choose<F: Family, T>(_: T, item: F::Item<T>, _: F) -> F::Item<T> {
    item
}

fn main() {
    let item = choose(Some(1), Some(2), Identity);
    assert_eq!(item, Some(2));
}
