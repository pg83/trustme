use std::cell::Cell;

struct Mark<'a>(&'a Cell<u64>, u64);

impl Drop for Mark<'_> {
    fn drop(&mut self) {
        self.0.set(self.0.get() * 10 + self.1);
    }
}

fn main() {
    let log = Cell::new(0);
    let moved = {
        let _outer = Mark(&log, 1);
        {
            let _inner = Mark(&log, 2);
            Mark(&log, 3)
        }
    };
    assert_eq!(log.get(), 21);
    drop(moved);
    assert_eq!(log.get(), 213);

    {
        let borrowed = &{
            let _local = Mark(&log, 4);
            Mark(&log, 5)
        };
        assert_eq!(borrowed.1, 5);
        assert_eq!(log.get(), 2134);
    }
    assert_eq!(log.get(), 21345);

    let selected = match Some(7u32) {
        Some(0 | 7) if {
            let _guard = Mark(&log, 6);
            false
        } => 0,
        Some(value) if {
            let _guard = Mark(&log, 7);
            value == 7
        } => value,
        _ => 0,
    };
    assert_eq!(selected, 7);
    assert_eq!(log.get(), 2134567);

    let entered = true && {
        let _right = Mark(&log, 8);
        true
    };
    assert!(entered);
    let result = loop {
        break {
            let _exit = Mark(&log, 9);
            91
        };
    };
    assert_eq!(result, 91);
    assert_eq!(log.get(), 213456789);
}
