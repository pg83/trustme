use std::cell::Cell;

struct Resource<'a>(u8, &'a Cell<u64>);

impl Drop for Resource<'_> {
    fn drop(&mut self) {
        let bit = 1u64 << self.0;
        assert_eq!(self.1.get() & bit, 0, "element dropped twice");
        self.1.set(self.1.get() | bit);
    }
}

#[inline(never)]
fn move_elements(choice: u8, dropped: &Cell<u64>) {
    let values: [_; 64] = std::array::from_fn(|i| Resource(i as u8, dropped));
    if choice == 0 {
        let [first, second, ..] = values;
        drop(first);
        assert_eq!(dropped.get(), 1);
        drop(second);
    } else if choice == 1 {
        let [.., penultimate, last] = values;
        drop(last);
        assert_eq!(dropped.get(), 1 << 63);
        drop(penultimate);
    } else if choice == 2 {
        drop(values);
    }
    assert_eq!(dropped.get(), match choice {
        0 => 3, 1 => 3 << 62, 2 => u64::MAX, _ => 0,
    });
}

fn main() {
    for choice in 0..4 {
        let dropped = Cell::new(0);
        move_elements(std::hint::black_box(choice), &dropped);
        assert_eq!(dropped.get(), u64::MAX);
    }
}
