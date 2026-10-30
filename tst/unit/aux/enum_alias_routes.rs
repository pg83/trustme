pub enum Event { Empty, Pair(u8, u16), Record { count: u16 } }
#[repr(u8)]
pub enum Code { Ready = 3, Busy = 17 }
pub mod facade {
    pub use super::Event as Message;
    pub use super::Code as Status;
}
