pub mod shapes {
    pub enum Record { Empty, Pair(u8, u8), Named { value: u8 } }
    pub enum Flag { Off = 3, On = 7 }
}
pub use shapes::{Record as Alias, Flag as FlagAlias};
