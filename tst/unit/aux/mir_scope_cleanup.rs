use std::cell::Cell;

struct Record<'a>(&'a Cell<u64>, u64);
impl Drop for Record<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() * 10 + self.1); }
}

#[inline]
pub fn scoped<F: FnOnce()>(log: &Cell<u64>, first: bool, finish: F) -> u64 {
    let left = Record(log, 1);
    let right = Record(log, 2);
    let selected = if first { left } else { right };
    {
        let _inner = Record(log, 3);
        drop(selected);
        finish();
    }
    log.get()
}
