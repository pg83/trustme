pub struct Flag;
pub const SELECTED: u16 = 97;
pub fn selected() -> u16 { SELECTED }
pub trait PatternMarker {}

pub mod facade {
    pub use super::Flag as PublicFlag;
    pub use super::SELECTED as PUBLIC_SELECTED;
}

pub mod nested {
    pub use super::facade::PublicFlag as VisibleFlag;
    pub use super::facade::PUBLIC_SELECTED as VISIBLE_SELECTED;
}
