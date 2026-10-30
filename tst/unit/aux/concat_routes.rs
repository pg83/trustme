#![feature(macro_metavar_expr_concat)]
#[macro_export]
macro_rules! routes {
    ($name:ident, $value:expr) => {
        pub fn ${concat($name, _value)}() -> u32 { $value }
        pub fn ${concat(read_, $name)}() -> u32 { $value + 1 }
    };
}
