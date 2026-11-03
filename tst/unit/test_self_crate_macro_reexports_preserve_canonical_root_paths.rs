pub extern crate self as owner;

#[macro_export]
macro_rules! number { () => { 73u32 }; }

mod route {
    pub use owner as local;
    pub use local::number as chosen;
}
mod nested {
    use crate::route::local::number as inside;
    pub fn read() -> u32 { inside!() }
}

fn main() {
    assert_eq!(route::chosen!(), 73);
    assert_eq!(nested::read(), 73);
}
