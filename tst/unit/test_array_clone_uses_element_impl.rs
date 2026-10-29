use std::cell::Cell;
use std::rc::Rc;
struct Item(u8, Rc<Cell<u32>>);
impl Clone for Item {
    fn clone(&self) -> Self {
        self.1.set(self.1.get() + 1);
        Item(self.0 + 1, self.1.clone())
    }
}
fn main() {
    let calls = Rc::new(Cell::new(0));
    let original = [Item(3, calls.clone()), Item(7, calls.clone())];
    let direct = original.clone();
    assert_eq!([direct[0].0, direct[1].0], [4, 8]);
    assert_eq!(calls.get(), 2);
    let pair = (original, Item(11, calls.clone()));
    let copied = pair.clone();
    assert_eq!([copied.0[0].0, copied.0[1].0, copied.1.0], [4, 8, 12]);
    assert_eq!(calls.get(), 5);
    let closure = move || pair.0[0].0 + pair.0[1].0;
    let copied_closure = closure.clone();
    assert_eq!(closure(), 10);
    assert_eq!(copied_closure(), 12);
    assert_eq!(calls.get(), 7);
}
