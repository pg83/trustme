pub enum Packet<T> { Empty, Data(T) }
pub use Packet::{Data as Build, Empty as Blank};
pub mod routed { pub use super::Build as Create; }
pub const MAKE: fn(u16) -> Packet<u16> = Build::<u16>;
#[macro_export]
macro_rules! builder { ($ty:ty) => { $crate::routed::Create::<$ty> }; }
