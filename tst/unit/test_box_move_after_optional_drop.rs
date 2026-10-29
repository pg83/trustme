use std::cell::Cell;
struct Count<'a>(&'a Cell<u32>);
impl Drop for Count<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
fn exercise(first: bool, second: bool) {
    let drops = Cell::new(0);
    {
        let boxed = Box::new(Count(&drops));
        if first { drop(boxed); }
        else if second { drop(*boxed); }
    }
    assert_eq!(drops.get(), 1);
}
fn main() {
    for first in [false, true] { for second in [false, true] { exercise(first, second); } }
}
