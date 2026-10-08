//@ aux-build: glob_prelude_dependency.rs
// A glob import takes each name with the smaller of the import's and the
// name's visibility (rustc_resolve's `try_define` of a glob binding, through
// `import.vis.min(binding.vis)` in `import`). The `extern crate std` rustc
// injects into a crate root is private, so `pub use crate::*;` in a public
// module does not export `std`: x509-parser's prelude does that, and its
// users glob-import the prelude.
use glob_prelude_dependency::prelude::*;

fn main() {
    let Certificate(n) = parse();
    assert_eq!(n, 7);
}
