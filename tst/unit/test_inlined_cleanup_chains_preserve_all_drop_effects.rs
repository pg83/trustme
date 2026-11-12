use std::cell::Cell;

struct Tick<'a>(&'a Cell<u32>, u32);

impl Drop for Tick<'_> {
    #[inline(always)]
    fn drop(&mut self) {
        self.0.set(self.0.get() + self.1);
        if std::hint::black_box(false) {
            panic!("drop interrupted");
        }
    }
}

#[inline(always)]
fn leaf(count: &Cell<u32>, stop: bool) {
    let _first = Tick(count, 1);
    let _second = Tick(count, 2);
    if stop {
        panic!("leaf interrupted");
    }
}

#[inline(always)]
fn middle(count: &Cell<u32>, stop: bool) {
    let _guard = Tick(count, 4);
    leaf(count, stop);
}

#[inline(never)]
fn entry<T>(payload: T, count: &Cell<u32>, stop: bool) {
    let _guard = Tick(count, 8);
    middle(count, stop);
    std::hint::black_box(payload);
}

fn main() {
    let count = Cell::new(0);
    entry([17u8; 3], &count, std::hint::black_box(false));
    assert_eq!(count.get(), 15);
}
