pub extern crate typed_glob_exports as original;
pub use original::nested::*;
pub use original as upstream;
pub mod second {
    pub use crate::original::nested::*;
    pub use crate::original as source;
}
