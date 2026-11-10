use std::cell::Cell;

struct Stamp<'a>(&'a Cell<u32>, u32);

impl Drop for Stamp<'_> {
    fn drop(&mut self) {
        self.0.set(self.0.get() + self.1);
    }
}

fn run(initial: bool) -> u32 {
    let log = Cell::new(0);
    let mut slot;
    if initial {
        slot = Stamp(&log, 101);
    }
    let mut step = 0;
    loop {
        step += 1;
        slot = Stamp(&log, step);
        drop(slot);
        if step == 3 {
            break;
        }
    }
    log.get()
}

fn main() {
    assert_eq!(run(false), 6);
    assert_eq!(run(true), 107);
}
