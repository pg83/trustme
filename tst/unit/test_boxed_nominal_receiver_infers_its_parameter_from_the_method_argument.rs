struct Gauge<T>(Option<T>);

impl<T> Gauge<T> {
    fn reading(self: Box<Self>, fallback: T) -> T {
        self.0.unwrap_or(fallback)
    }
}

fn main() {
    let empty = Box::new(Gauge(None));
    assert_eq!(empty.reading(313u16), 313);
    let full = Box::new(Gauge(Some(317u16)));
    assert_eq!(full.reading(319), 317);
}
