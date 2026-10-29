use std::cell::Cell;

struct Guard<'a>(&'a Cell<u32>);
impl Drop for Guard<'_> {
    fn drop(&mut self) {
        self.0.set(self.0.get() + 1);
    }
}

trait Discard {
    fn discard(self) -> u32;
}
impl<F: FnOnce() -> ()> Discard for F {
    fn discard(self) -> u32 { 11 }
}

fn main() {
    let drops = Cell::new(0);
    let guard = Guard(&drops);
    let callback = if true {
        move || { drop(guard); panic!("must not run"); }
    } else {
        loop {}
    };
    assert_eq!(drops.get(), 0);
    assert_eq!(callback.discard(), 11);
    assert_eq!(drops.get(), 1);
}
