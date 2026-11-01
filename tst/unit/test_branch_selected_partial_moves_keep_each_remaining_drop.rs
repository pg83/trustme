//@ compile-variant: -O
use std::cell::Cell;

struct Count<'a> { value: u16, events: &'a Cell<u16> }
impl Drop for Count<'_> {
    fn drop(&mut self) { self.events.set(self.events.get() + self.value); }
}
#[inline(never)]
fn select(first: bool, events: &Cell<u16>) {
    let pair = (Count { value: 3, events }, Count { value: 5, events });
    let selected = if first { pair.0 } else { pair.1 };
    drop(selected);
    assert_eq!(events.get(), if first { 3 } else { 5 });
}
fn main() {
    let left = Cell::new(0);
    select(true, &left);
    assert_eq!(left.get(), 8);
    let right = Cell::new(0);
    select(false, &right);
    assert_eq!(right.get(), 8);
}
