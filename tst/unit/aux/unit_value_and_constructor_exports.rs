pub struct Unit;
pub struct Payload(pub u8);
pub const UNIT: Unit = Unit;
pub static EXPOSED: Unit = Unit;
pub const MAKE: fn(u8) -> Payload = Payload;
