use std::cell::Cell;

struct Stamp<'a>(&'a Cell<u32>, u32);

impl Drop for Stamp<'_> {
    fn drop(&mut self) {
        self.0.set(self.0.get() * 10 + self.1);
    }
}

fn leave(trace: &Cell<u32>, fail: bool) {
    let _outer = Stamp(trace, 1);
    loop {
        let _inner = Stamp(trace, 2);
        if fail {
            panic!("potential cleanup path");
        }
        break;
    }
}

fn main() {
    let trace = Cell::new(0);
    leave(&trace, std::hint::black_box(false));
    assert_eq!(trace.get(), 21);
}
