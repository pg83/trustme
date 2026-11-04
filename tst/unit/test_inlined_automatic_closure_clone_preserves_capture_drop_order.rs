//@ compile-flags: -Cpanic=unwind -Zmir-opt-level=3
//@ compile-variant: -O
use std::cell::Cell;
use std::rc::Rc;

struct Mark(Rc<Cell<u32>>, u32);
impl Clone for Mark {
    fn clone(&self) -> Self {
        self.0.set(self.0.get() * 10 + self.1);
        Self(self.0.clone(), self.1)
    }
}
impl Drop for Mark {
    fn drop(&mut self) { self.0.set(self.0.get() * 10 + self.1); }
}
fn read(value: &Mark) -> u32 { value.1 }

fn main() {
    let log = Rc::new(Cell::new(0));
    let first = Mark(log.clone(), 3);
    let second = Mark(log.clone(), 7);
    let original = move || read(&first) + read(&second);
    let copied = original.clone();
    assert_eq!(log.get(), 37);
    assert_eq!(original(), 10);
    assert_eq!(copied(), 10);
    drop(copied);
    assert_eq!(log.get(), 3737);
    drop(original);
    assert_eq!(log.get(), 373737);
}
