//@ compile-fail: cannot be lowered here
// A `forbid` may not be lifted: an `allow`, `warn` or `deny` for the same lint
// nested inside the item that forbade it is an error, whether or not the lint
// would ever fire.
//
// Same shape as the Rust Reference example attributes/diagnostics.md:92.
#![allow(unused)]

fn main() {
    #[forbid(missing_docs)]
    pub mod m3 {
        // Attempting to toggle the lint signals an error here.
        #[allow(missing_docs)] //~ ERROR
        /// Returns 2.
        pub fn undocumentedToo() -> i32 {
            2
        }
    }
}
