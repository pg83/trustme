#![feature(decl_macro)]
mod values {
    pub const NUMBER: u32 = 19;
    pub fn identity<T>(value: T) -> T { value }
}
macro_rules! define_path {
    ($name:ident, $path:path) => {
        macro $name() { $path }
    };
}
define_path!(number, values::NUMBER);
define_path!(function, values::identity::<u16>);
fn main() {
    assert_eq!(number!(), 19);
    let identity: fn(u16) -> u16 = function!();
    assert_eq!(identity(37), 37);
}
