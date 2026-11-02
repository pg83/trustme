//@ compile-variant: -O
struct Payload(u8);
trait Family { type Item; }
struct Kind;
impl Family for Kind { type Item = Payload; }
trait Read { fn read(&self) -> u8; }
impl Read for <Kind as Family>::Item { fn read(&self) -> u8 { self.0 } }
fn main() { assert_eq!(Payload(47).read(), 47); }
