#![no_std]

#[macro_export]
macro_rules! add_six {
    ($value:expr) => {
        $value + 6u16
    };
}

#[macro_export]
macro_rules! define_score {
    ($name:ident) => {
        pub const $name: u16 = 19;
    };
}
