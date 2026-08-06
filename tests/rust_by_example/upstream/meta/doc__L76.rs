// Extracted from src/meta/doc.md:76
#![allow(unused)]
fn main() {
    #[doc(inline)]
    pub use bar::Bar;
    
    /// bar docs
    pub mod bar {
        /// the docs for Bar
        pub struct Bar;
    }
}
