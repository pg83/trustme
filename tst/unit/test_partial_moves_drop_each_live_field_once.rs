use std::cell::Cell;
struct Tracked<'a> { log: &'a Cell<u32>, digit: u32 }
impl Drop for Tracked<'_> { fn drop(&mut self) { self.log.set(self.log.get() * 10 + self.digit); } }
struct Pair<'a> { first: Tracked<'a>, second: Tracked<'a> }
fn consume(early: bool) -> u32 {
    let log = Cell::new(0);
    {
        let pair = Pair { first: Tracked { log: &log, digit: 1 }, second: Tracked { log: &log, digit: 2 } };
        if early { drop(pair.first); }
        drop(pair.second);
    }
    log.get()
}
fn main() { assert_eq!(consume(true), 12); assert_eq!(consume(false), 21); }
