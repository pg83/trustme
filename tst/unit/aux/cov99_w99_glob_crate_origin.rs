pub mod declarations {
    pub extern crate core as exposed;
}

pub mod indexed {
    pub use crate::declarations::*;
}
