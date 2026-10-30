mod definitions {
    #[derive(Debug, PartialEq)]
    pub enum Signal { Idle, Value(u16), Named { code: u8 } }
}
mod bridge { pub use super::definitions::Signal::*; }
use bridge::{Idle, Named as Record, Value as Make};
fn main() {
    assert_eq!(Idle, definitions::Signal::Idle);
    assert_eq!(Make(17), definitions::Signal::Value(17));
    assert_eq!(Record { code: 23 }, definitions::Signal::Named { code: 23 });
}
