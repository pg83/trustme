struct Zst;

impl Zst {
    fn touch(&mut self) {}
}

fn insert_and_borrow<T, F: FnOnce() -> T>(slot: &mut Option<T>, make: F) -> &mut T {
    *slot = Some(make());
    slot.as_mut().unwrap()
}

fn main() {
    let mut slot = None;
    insert_and_borrow(&mut slot, || [Zst])[0].touch();
}
