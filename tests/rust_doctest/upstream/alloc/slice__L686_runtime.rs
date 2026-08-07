// Extracted from library/alloc/src/slice.rs:686
#![allow(unused)]
extern crate alloc;
fn main() {
    #[allow(dead_code)]
    pub struct Foo(Vec<u32>, Vec<String>);

    impl std::borrow::Borrow<[u32]> for Foo {
        fn borrow(&self) -> &[u32] { &self.0 }
    }

    impl std::borrow::Borrow<[String]> for Foo {
        fn borrow(&self) -> &[String] { &self.1 }
    }
}
