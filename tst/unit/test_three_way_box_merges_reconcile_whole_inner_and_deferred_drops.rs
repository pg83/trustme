//@ compile-variant: -O
use std::cell::Cell;
struct Mark<'a>(&'a Cell<u32>);
impl Drop for Mark<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
fn main() {
    for branch in 0..3 {
        let events = Cell::new(0);
        {
            let owner = Box::new(Mark(&events));
            match std::hint::black_box(branch) {
                0 => drop(owner),
                1 => (),
                _ => drop(*owner),
            }
        }
        assert_eq!(events.get(), 1);
    }
}
