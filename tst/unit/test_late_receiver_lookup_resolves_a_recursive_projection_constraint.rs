trait Peel {
    type Item;
}
impl Peel for (u16,) {
    type Item = u16;
}

struct Hole<T>(Option<T>);
impl Hole<(u16,)> {
    fn verify(self) -> bool {
        self.0.is_none()
    }
}

fn tie<T: Peel>(_: Hole<T>) -> Hole<(T::Item,)> {
    Hole(None)
}

fn main() {
    let mut value = Hole(None);
    value = tie(value);
    assert!(value.verify());
}
