struct Sender<T>(Option<T>);

impl<T> Sender<T> {
    fn is_empty(&self) -> bool {
        self.0.is_none()
    }
}

fn bounded<T>() -> (Sender<T>, u8) {
    (Sender(None), 0)
}

fn main() {
    let s1 = Sender::<()>(None);
    let (hole, _r) = bounded();
    let s1 = if s1.is_empty() { &s1 } else { &hole };
    let s: &Sender<_> = &s1;
    assert!(s.is_empty());
}
