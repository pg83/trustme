// A crate whose prelude re-exports its root by a glob, as x509-parser's
// `pub mod prelude { ... pub use crate::*; }` does.
pub mod prelude {
    pub use crate::*;
}

pub struct Certificate(pub u32);

pub fn parse() -> Certificate {
    Certificate(7)
}
