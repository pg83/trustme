//@ compile-variant: -O
use std::cell::Cell;
struct Payload<'a>(&'a Cell<u8>);
type Alias<'a> = Payload<'a>;
impl Drop for Alias<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 1); } }
fn main() {
    let count = Cell::new(0);
    let value: Alias<'_> = Payload(&count);
    drop(value);
    assert_eq!(count.get(), 1);
}
