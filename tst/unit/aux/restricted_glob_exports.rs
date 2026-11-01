mod source { pub struct Token(pub u16); }
pub mod container {
    pub mod api {
        pub(in crate::container) use crate::source::*;
        pub(self) use crate::source::*;
        pub use crate::source::*;
    }
}
