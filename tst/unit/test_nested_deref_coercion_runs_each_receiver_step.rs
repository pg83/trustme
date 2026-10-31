use std::cell::Cell;
use std::ops::Deref;
struct Layer<'a, T> { value: T, visits: &'a Cell<u8> }
impl<T> Deref for Layer<'_, T> {
    type Target = T;
    fn deref(&self) -> &T { self.visits.set(self.visits.get() + 1); &self.value }
}
fn read(value: &u16) -> u16 { *value }
fn main() {
    let visits = Cell::new(0);
    let inner = Layer { value: 41u16, visits: &visits };
    let outer = Layer { value: inner, visits: &visits };
    assert_eq!(read(&outer), 41);
    assert_eq!(visits.get(), 2);
}
