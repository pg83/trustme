//@ compile-fail: error:
trait Layer {
    type Item;
}

impl<T> Layer for (u8, T) {
    type Item = T;
}

impl<T> Layer for (u16, T) {
    type Item = T;
}

fn step<T: Layer>(_: Option<T>) -> Option<(T::Item, T::Item)> {
    None
}

fn main() {
    let mut value = None;
    value = step(value);
    let _ = value;
}
