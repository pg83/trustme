// usvg: crate-root `pub use parser::*` must not expose the *private*
// `parser::filter`; the public `tree::filter` must win.
mod tree { pub mod filter { pub fn which() -> u32 { 1 } } }
mod parser { mod filter { pub fn which() -> u32 { 2 } } pub fn go() -> u32 { filter::which() } }
pub use tree::*;   // public filter
pub use parser::*; // private filter must not shadow it
fn main() {
    assert_eq!(filter::which(), 1);  // the public one
    assert_eq!(parser::go(), 2);     // parser still sees its own private one
}
