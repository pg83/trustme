use std::cell::Cell;

struct Value(u8);
impl Value {
    #[inline(never)]
    fn choose(flag: bool) -> Self {
        let selected = if flag { Self(23) } else { Self(47) };
        selected
    }
    fn identity(self) -> Self { self }
}

struct Empty {}
struct Record<'a>(&'a Cell<u32>, u32);
impl Drop for Record<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() * 10 + self.1); }
}

#[inline(never)]
fn select_record(log: &Cell<u32>, flag: bool) -> Record<'_> {
    let first = Record(log, 1);
    let selected = if flag { Record(log, 2) } else { Record(log, 3) };
    drop(first);
    selected
}

fn main() {
    assert_eq!(Value::choose(std::hint::black_box(true)).identity().0, 23);
    assert_eq!(Value::choose(std::hint::black_box(false)).identity().0, 47);
    let log = Cell::new(0);
    drop(select_record(&log, true));
    assert_eq!(log.get(), 12);
    log.set(0);
    drop(select_record(&log, false));
    assert_eq!(log.get(), 13);
    let empty = std::hint::black_box(Empty {});
    assert_eq!(std::mem::size_of_val(&empty), 0);
}
