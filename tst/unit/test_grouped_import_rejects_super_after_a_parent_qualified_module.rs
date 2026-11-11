//@ compile-fail: `super` is only allowed at the start of a path
mod outer {
    pub const ROOT: u16 = 163;
    pub mod nested {}
    pub mod inner {
        use super::nested::{super::ROOT as IMPORTED};
        pub fn read() -> u16 { IMPORTED }
    }
}

fn main() {
    assert_eq!(outer::inner::read(), 163);
}
