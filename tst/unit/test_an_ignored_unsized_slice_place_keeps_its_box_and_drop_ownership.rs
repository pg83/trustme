use std::cell::Cell;
struct Token<'a>(u8, &'a Cell<u8>);
impl Drop for Token<'_> {
    fn drop(&mut self) {
        let bit = 1 << self.0;
        assert_eq!(self.1.get() & bit, 0);
        self.1.set(self.1.get() | bit);
    }
}
fn main() {
    let dropped = Cell::new(0);
    let values: Box<[Token<'_>]> = (0..3).map(|i| Token(i, &dropped)).collect::<Vec<_>>().into_boxed_slice();
    let _ = *values;
    assert_eq!(dropped.get(), 0);
    assert_eq!(values.len(), 3);
    drop(values);
    assert_eq!(dropped.get(), 7);
}
