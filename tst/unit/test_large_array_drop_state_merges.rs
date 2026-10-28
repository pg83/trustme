use std::cell::Cell;

struct Resource<'a>(&'a Cell<usize>);
impl Drop for Resource<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() + 1); }
}

#[inline(never)]
fn optional_then_partial(choice: u8, count: &Cell<usize>) {
    let values: [_; 32] = std::array::from_fn(|_| Resource(count));
    match choice {
        0 => {},
        1 => drop(values),
        _ => { let [first, ..] = values; drop(first); },
    }
    assert_eq!(count.get(), match choice { 0 => 0, 1 => 32, _ => 1 });
}

#[inline(never)]
fn invalid_then_partial(choice: bool, count: &Cell<usize>) {
    let values: [_; 32] = std::array::from_fn(|_| Resource(count));
    if choice { drop(values); } else {
        let [.., last] = values;
        drop(last);
    }
    assert_eq!(count.get(), if choice { 32 } else { 1 });
}

#[inline(never)]
fn replace_after_partial(iterations: usize, count: &Cell<usize>) {
    let mut values: [_; 32] = std::array::from_fn(|_| Resource(count));
    let [first, ..] = values;
    for i in 0..iterations {
        values = std::array::from_fn(|_| Resource(count));
        assert_eq!(count.get(), 31 + 32 * i);
    }
    drop(first);
}

fn main() {
    for choice in 0..3 {
        let count = Cell::new(0);
        optional_then_partial(std::hint::black_box(choice), &count);
        assert_eq!(count.get(), 32);
    }
    for choice in [false, true] {
        let count = Cell::new(0);
        invalid_then_partial(std::hint::black_box(choice), &count);
        assert_eq!(count.get(), 32);
    }
    for iterations in 0..4 {
        let count = Cell::new(0);
        replace_after_partial(std::hint::black_box(iterations), &count);
        assert_eq!(count.get(), 32 * (iterations + 1));
    }
}
