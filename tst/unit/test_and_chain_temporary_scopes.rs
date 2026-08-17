// Each `&&` operand is its own temporary scope: the left one's temporaries drop
// before the right one is evaluated, so they drop in the order they were made.
use std::cell::RefCell;

#[derive(Default)]
struct Log(RefCell<Vec<u32>>);

struct Loud<'a>(&'a Log, u32);

impl Drop for Loud<'_> {
    fn drop(&mut self) {
        self.0.0.borrow_mut().push(self.1);
    }
}

impl Log {
    fn loud(&self, n: u32) -> Option<Loud<'_>> {
        Some(Loud(self, n))
    }
    fn mark(&self, n: u32) {
        self.0.borrow_mut().push(n)
    }
}

fn main() {
    let log = Log::default();
    if log.loud(1).is_some() && log.loud(2).is_some() {
        log.mark(3);
    }
    assert_eq!(*log.0.borrow(), [1, 2, 3]);

    let log = Log::default();
    if log.loud(1).is_some() && log.loud(2).is_some() && let Some(_d) = log.loud(4) {
        log.mark(3);
    }
    assert_eq!(*log.0.borrow(), [1, 2, 3, 4]);
}
