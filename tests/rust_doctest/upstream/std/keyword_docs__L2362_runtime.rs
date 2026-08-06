// Extracted from library/std/src/keyword_docs.rs:2362
#![allow(unused)]
#![allow(dead_code)]
fn main() {
    pub enum Cow<'a, B>
    where
        B: ToOwned + ?Sized,
    {
        Borrowed(&'a B),
        Owned(<B as ToOwned>::Owned),
    }
}
