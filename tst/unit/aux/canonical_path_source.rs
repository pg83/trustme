pub extern crate self as root;
pub use root as again;

pub mod source {
    pub mod inner {
        pub struct Number(pub u32);
        pub fn read(value: &Number) -> u32 { value.0 }
    }
}

pub use source as front;
pub use front::inner as visible;
pub use core::concat as words;

#[macro_export]
macro_rules! number {
    ($value:expr) => { $crate::visible::Number($value) };
}
