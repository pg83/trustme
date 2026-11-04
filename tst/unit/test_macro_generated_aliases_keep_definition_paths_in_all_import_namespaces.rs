//@ compile-variant: -O
pub extern crate self as root;

macro_rules! declare {
    ($module:ident, $alias:ident) => {
        pub mod $module {
            pub mod leaf {
                pub struct Number(pub u32);
                pub fn read(number: &Number) -> u32 { number.0 }
            }
            pub use self::leaf as r#type;
        }
        pub use $module::r#type as $alias;
    };
}

declare!(declared, exposed);

#[macro_use]
mod factory {
    macro_rules! number {
        ($value:expr) => { $crate::root::exposed::Number($value) };
    }
}

mod routes {
    pub use crate::root::exposed as first;
    pub use self::first as second;
}

fn main() {
    use routes::second::{Number, read};
    assert_eq!(read(&Number(89)), 89);
    assert_eq!(read(&number!(97)), 97);
    {
        use crate::root::routes::first::*;
        assert_eq!(read(&Number(101)), 101);
    }
}
