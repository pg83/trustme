#![feature(decl_macro)]

pub mod origin {
    pub mod inner {
        pub const CODE: u16 = 277;
        pub struct Stamp(pub u16);
    }
}
pub use origin as first;

pub mod facade {
    pub use crate::first as second;
    pub macro bring($name:ident) {
        use second::inner::Stamp as $name;
    }
}
pub use facade as entrance;
pub use entrance::second::inner as route;
