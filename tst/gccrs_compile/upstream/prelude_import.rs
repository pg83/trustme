
#![feature(prelude_import)]

mod core {
    pub mod prelude {
        pub mod v1 {
            // hehe
        }
    }
}

#[prelude_import]
use core::prelude::v1::*;
