#![feature(prelude_import)]

mod local_prelude {
    pub trait Measure {
        fn measure(&self) -> u16;
    }
    impl Measure for u16 {
        fn measure(&self) -> u16 { *self + 7 }
    }
}

#[prelude_import]
use self::local_prelude::*;

mod client {
    pub fn measured() -> u16 {
        19u16.measure()
    }
}

fn main() {
    std::assert_eq!(client::measured(), 26);
}
