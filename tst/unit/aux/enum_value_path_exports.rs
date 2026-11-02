#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Flag { Off = 2, On = 7 }
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Packet<T> { Empty, Data(T), Zero() }
pub use Flag::On as ACTIVE;
pub use Packet::Empty as NONE;
pub use Packet::Zero as NEW_ZERO;
pub const SELECTED: Flag = ACTIVE;
pub static CURRENT: Flag = Flag::Off;
pub const fn choose(enabled: bool) -> Flag { if enabled { ACTIVE } else { Flag::Off } }
