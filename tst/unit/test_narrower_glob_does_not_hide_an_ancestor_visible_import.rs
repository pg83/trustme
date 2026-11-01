mod outer {
    mod source { pub struct Token(pub u16); }
    mod api {
        pub(in crate::outer) use super::source::*;
        pub(self) use super::source::*;
    }
    pub fn value() -> u16 { api::Token(17).0 }
}
fn main() { assert_eq!(outer::value(), 17); }
