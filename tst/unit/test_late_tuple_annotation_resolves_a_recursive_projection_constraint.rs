trait Peel {
    type Item;
}
impl Peel for (u16,) {
    type Item = u16;
}

fn tie<T: Peel>(_: Option<T>) -> Option<(T::Item,)> {
    None
}

fn main() {
    let mut value = None;
    value = tie(value);
    let constrained: Option<(u16,)> = value;
    assert!(constrained.is_none());
}
