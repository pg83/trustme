//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// pest_derive emits character literals such as `'\x7f'` as source text that
// `quote` parses with `TokenStream::from_str`. Our proc_macro lexer knew
// `\x` escapes in strings but not in character literals and panicked.

use proc_macro_item_passthrough::hex_escaped_char;

fn main() {
    assert_eq!(hex_escaped_char!(), 'A');
}
