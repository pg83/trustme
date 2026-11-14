//@ compile-flags: -Z dump-ast
//@ output-contains: _1_ast.rs b"\r"

fn main() {
    let bytes = b"\r";
    assert!(bytes[0] == 13);
}
