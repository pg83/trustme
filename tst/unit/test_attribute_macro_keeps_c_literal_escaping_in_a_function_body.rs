//@ proc-macro-aux-build: late_body_identity.rs
//@ compile-flags: -Zdump-ast
//@ output-contains: _1_ast.rs c"attribute
use late_body_identity::keep;

#[keep]
fn main() {
    let text = "attribute\nβ\"\\";
    let bytes = b"attribute\0\xff\"\\";
    let c = c"attribute\n\"\\";
    assert_eq!(text, "attribute\nβ\"\\");
    assert_eq!(bytes, b"attribute\0\xff\"\\");
    assert_eq!(c.to_bytes(), b"attribute\n\"\\");
}
