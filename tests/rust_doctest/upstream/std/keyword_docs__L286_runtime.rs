// Extracted from library/std/src/keyword_docs.rs:286
#![allow(unused)]
fn main() {
    #[allow(unused_imports)]
    pub(crate) use std::io::Error as IoError;
    pub(crate) enum CoolMarkerType { }
    pub struct PublicThing {
        pub(crate) semi_secret_thing: bool,
    }
}
