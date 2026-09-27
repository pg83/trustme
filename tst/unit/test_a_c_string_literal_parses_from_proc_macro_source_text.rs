//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// indoc prints a literal of its input, unindents the text and parses it back
// with `Literal::from_str`. Upstream's lexer reads C string literals, plain
// and raw (`c"..."`, `cr#"..."#`); ours did not know the `c` prefix and the
// parse failed ("Wasn't a literal").
use proc_macro_item_passthrough::reparse_literals;
use std::ffi::CStr;

fn main() {
    let plain: &CStr = reparse_literals!(c"x\n");
    assert_eq!(plain.to_bytes(), b"x\n");
    let raw: &CStr = reparse_literals!(cr#"a"b"#);
    assert_eq!(raw.to_bytes(), b"a\"b");
}
