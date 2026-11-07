use std::cell::Cell;

struct Token<'a> {
    drops: &'a Cell<u16>,
    value: u16,
}

impl Drop for Token<'_> {
    fn drop(&mut self) {
        self.drops.set(self.drops.get() + 1);
    }
}

fn main() {
    let drops = Cell::new(0);
    {
        let ref value = loop {
            if std::hint::black_box(false) {
                break Token { drops: &drops, value: 503 };
            }
            break Token { drops: &drops, value: 509 }
        };
        assert_eq!(value.value, 509);
        assert_eq!(drops.get(), 0);
    }
    assert_eq!(drops.get(), 1);
    {
        let value = &loop {
            if std::hint::black_box(true) {
                break Token { drops: &drops, value: 521 };
            }
            break Token { drops: &drops, value: 523 }
        };
        assert_eq!(value.value, 521);
        assert_eq!(drops.get(), 1);
    }
    assert_eq!(drops.get(), 2);
}
