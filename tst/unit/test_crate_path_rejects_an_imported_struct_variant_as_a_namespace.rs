//@ compile-fail: Encountered non-namespace item 'Record'
enum Packet { Record { code: u8 } }
use Packet::Record;
fn main() { let _ = crate::Record::missing; }
