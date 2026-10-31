trait Value {
    type Item;
    fn read(&self) -> Option<Self::Item>;
}

impl<T> Value for Option<T> {
    type Item = T;
    fn read(&self) -> Option<T> { None }
}

trait ReadOr: Value {
    fn read_or(&self, fallback: Self::Item) -> Self::Item {
        self.read().unwrap_or(fallback)
    }
}

impl<T: Value + ?Sized> ReadOr for T {}

fn main() {
    let source = None;
    let object: &(dyn Value<Item = _> + Send) = &source;
    assert_eq!(object.read_or(43u16), 43u16);
}
