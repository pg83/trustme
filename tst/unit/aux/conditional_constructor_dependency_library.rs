static FLAG: u32 = 37;
pub struct Record(pub u32);
pub enum Choice { Packet(u32), Empty }
pub fn parts<T: Copy>(value: T) -> (T, &'static u32, fn(u32) -> Record, fn(u32) -> Choice) {
    (value, &FLAG, Record, Choice::Packet)
}
