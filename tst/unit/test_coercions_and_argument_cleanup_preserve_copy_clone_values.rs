use std::cell::Cell;

#[derive(Clone, Copy)]
struct Word(u16);

struct Guard<'a>(&'a Cell<u16>);

impl Drop for Guard<'_> {
    fn drop(&mut self) {
        self.0.set(self.0.get() + 3);
    }
}

fn copied<T: Copy + Clone>(value: T) -> (T, T) {
    (value, value.clone())
}

fn selected(mut values: (Word, Word), _guard: Guard<'_>, early: bool) -> u16 {
    let words = [values.0, values.1];
    let slice: &[Word] = &words;
    let read: fn(Word) -> u16 = |value| value.0;
    if early {
        return read(slice[1]);
    }
    values.0 = values.1;
    read(values.0)
}

fn main() {
    let drops = Cell::new(0);
    let (first, second) = copied(Word(571));
    assert_eq!(first.0, second.0);
    assert_eq!(selected((first, Word(577)), Guard(&drops), true), 577);
    assert_eq!(drops.get(), 3);
    assert_eq!(selected((second, Word(587)), Guard(&drops), false), 587);
    assert_eq!(drops.get(), 6);
}
