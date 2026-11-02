//@ compile-variant: -O
use std::cell::Cell;
use std::ops::Deref;

struct Tail([u16; 2]);
struct Branch<'a> { value: Tail, visits: &'a Cell<u16> }
struct Other<'a>(Branch<'a>);
impl Deref for Tail {
    type Target = [u16; 2];
    fn deref(&self) -> &Self::Target { &self.0 }
}
impl Deref for Branch<'_> {
    type Target = Tail;
    fn deref(&self) -> &Tail {
        self.visits.set(self.visits.get() + 1);
        &self.value
    }
}
impl<'a> Deref for Other<'a> {
    type Target = Branch<'a>;
    fn deref(&self) -> &Branch<'a> {
        self.0.visits.set(self.0.visits.get() + 10);
        &self.0
    }
}

fn main() {
    let visits = Cell::new(0);
    let left = Branch { value: Tail([17, 19]), visits: &visits };
    let right = Other(Branch { value: Tail([23, 29]), visits: &visits });
    for first in [true, false] {
        visits.set(0);
        let selected: &Tail = if first { &left } else { &right };
        assert_eq!(selected.0[0], if first { 17 } else { 23 });
        assert_eq!(visits.get(), if first { 1 } else { 11 });
    }
}
