use std::cell::Cell;

struct Mark<'a>(&'a Cell<u32>, u32);

impl Drop for Mark<'_> {
    fn drop(&mut self) {
        self.0.set(self.0.get() * 10 + self.1);
    }
}

fn choose(log: &Cell<u32>) -> u8 {
    let _outer = Mark(log, 1);
    'search: loop {
        let _iteration = Mark(log, 2);
        match Some(Mark(log, 4)) {
            Some(ref value) if {
                let _guard = Mark(log, 8);
                value.1 == 4
            } => break 'search 13,
            _ => continue 'search,
        }
    }
}

fn main() {
    let log = Cell::new(0);
    assert_eq!(choose(&log), 13);
    assert_eq!(log.get(), 8421);
}
