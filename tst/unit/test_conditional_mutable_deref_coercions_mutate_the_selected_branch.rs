//@ compile-variant: -O
use std::cell::Cell;
use std::ops::{Deref, DerefMut};
struct Inner(u16);
struct Outer<'a>(Inner, &'a Cell<u16>);
impl Deref for Outer<'_> {
    type Target = Inner;
    fn deref(&self) -> &Inner { &self.0 }
}
impl DerefMut for Outer<'_> {
    fn deref_mut(&mut self) -> &mut Inner {
        self.1.set(self.1.get() + 1);
        &mut self.0
    }
}

fn main() {
    for first in [true, false] {
        let visits = Cell::new(0);
        let mut outer = Outer(Inner(17), &visits);
        let mut inner = Inner(23);
        let selected = if first { &mut outer } else { &mut inner };
        selected.0 += 10;
        assert_eq!(outer.0.0, if first { 27 } else { 17 });
        assert_eq!(inner.0, if first { 23 } else { 33 });
        assert_eq!(visits.get(), if first { 1 } else { 0 });

        let selected = if first { &mut inner } else { &mut outer };
        selected.0 += 1;
        assert_eq!(outer.0.0, if first { 27 } else { 18 });
        assert_eq!(inner.0, if first { 24 } else { 33 });
        assert_eq!(visits.get(), 1);
    }
}
