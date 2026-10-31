use std::cell::Cell;

struct Token<'a>(&'a Cell<u64>, u64);
impl Drop for Token<'_> {
    fn drop(&mut self) {
        assert_eq!(self.0.get() & self.1, 0);
        self.0.set(self.0.get() | self.1);
    }
}
fn token(log: &Cell<u64>, index: u32) -> Token<'_> { Token(log, 1u64 << index) }
