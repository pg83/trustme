//@ edition: 2018
//@ aux-build: namespace_root_macro.rs

mod facade {
    pub use namespace_root_macro as Root;
    pub mod nested {
        pub use super::Root as Again;
    }
}

use facade::nested::Again::root_value as indirect;

fn main() {
    assert_eq!(facade::Root::root_value!(), 601);
    assert_eq!(facade::nested::Again::root_value!(), 601);
    assert_eq!(indirect!(), 601);
}
