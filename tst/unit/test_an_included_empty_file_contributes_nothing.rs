// mime_guess (under tower-http) includes a table its build script writes,
// and with its `phf` and `rev-mappings` features off the script writes
// nothing. rustc takes an empty source file as one with no items; our lexer
// read the first byte to look for a byte-order mark and threw
// `Lexer::EndOfFile` out of its constructor.
include!("aux/empty_include.rs");

fn main() {
    assert_eq!(2 + 2, 4);
}
