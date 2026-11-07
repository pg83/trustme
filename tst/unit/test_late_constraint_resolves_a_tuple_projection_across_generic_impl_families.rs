trait Layer {
    type Item;
}

impl<T> Layer for (T,) {
    type Item = T;
}

impl<T> Layer for [T; 1] {
    type Item = T;
}

fn step<T: Layer>(_: Option<T>) -> Option<(T::Item,)> {
    None
}

fn main() {
    let mut value = None;
    value = step(value);
    let checked: Option<(u16,)> = value;
    assert!(checked.is_none());
}
