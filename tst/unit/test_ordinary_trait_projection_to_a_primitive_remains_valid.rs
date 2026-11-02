//@ compile-variant: -O
trait Family { type Item; }
struct Kind;
impl Family for Kind { type Item = u16; }
trait Read { fn read(&self) -> u16; }
impl Read for <Kind as Family>::Item { fn read(&self) -> u16 { *self } }
fn main() { assert_eq!(53u16.read(), 53); }
