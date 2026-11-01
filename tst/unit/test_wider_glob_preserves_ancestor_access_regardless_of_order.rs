mod outer {
    mod source { pub struct Token(pub u16); }
    mod api {
        pub(self) use super::source::*;
        pub(in crate::outer) use super::source::*;
    }
    pub fn value() -> u16 { api::Token(23).0 }
}
fn main() { assert_eq!(outer::value(), 23); }
