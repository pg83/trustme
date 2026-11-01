pub mod inside {
    pub struct Record(pub u16);
    pub enum Choice { Unit, Tuple(u16), Named { value: u8 } }
    pub static TOTAL: u16 = 13;
    pub const NUMBER: u16 = 19;
    pub fn add(value: u16) -> u16 { value + 3 }
}

#[macro_export]
macro_rules! token { ($value:expr) => { $value + 5u16 }; }
