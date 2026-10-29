use std::cell::Cell;
struct Tracked<'a>(&'a Cell<usize>);
impl Drop for Tracked<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() + 1); }
}
fn exercise(active: bool) -> usize {
    let count = Cell::new(0);
    {
        let array: [Tracked<'_>; 64];
        if active {
            array = std::array::from_fn(|_| Tracked(&count));
            let [first, .., last] = array;
            drop(first);
            drop(last);
        }
    }
    count.get()
}
fn main() {
    assert_eq!(exercise(false), 0);
    assert_eq!(exercise(true), 64);
}
