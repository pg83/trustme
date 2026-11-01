#![recursion_limit = "4"]

macro_rules! build {
    ($name:ident, $value:expr) => { mod $name { build!(@middle $value); } };
    (@middle $value:expr) => { build!(@finish $value); };
    (@finish $value:expr) => { pub const VALUE: u16 = $value; };
}

build!(left, 11);
build!(right, 17);

fn main() { assert_eq!(left::VALUE + right::VALUE, 28); }
