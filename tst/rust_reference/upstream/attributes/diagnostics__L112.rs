// Extracted from src/attributes/diagnostics.md:112
#![allow(unused)]
#![deny(
fn main() {
    // `keyword_idents` is allowed by default. Here we deny it to
    // avoid migration of identifiers when we update the edition.
        keyword_idents,
        reason = "we want to avoid these idents to be future compatible"
    )]
    
    // This name was allowed in Rust's 2015 edition. We still aim to avoid
    // this to be future compatible and not confuse end users.
    fn dyn() {}
}
