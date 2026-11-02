pub struct Unit;
pub struct Empty();
pub enum Choice { Unit, Empty(), Value(u16) }
pub use Choice as AliasChoice;
pub use Empty as AliasEmpty;
