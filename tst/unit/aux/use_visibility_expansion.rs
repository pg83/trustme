mod source { pub struct Token(pub u16); }
pub mod api {
    pub(crate) use crate::source::*;
    pub use crate::source::*;
}
