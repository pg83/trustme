pub enum Shape<T> { Point(T), Void }
pub use Shape::Point as MakePoint;
pub use Shape::Void as MakeVoid;
pub struct Packet<const N: usize>(pub [u8; N]);
pub use Packet as Reexported;
pub static VALUE: u32 = 53;
