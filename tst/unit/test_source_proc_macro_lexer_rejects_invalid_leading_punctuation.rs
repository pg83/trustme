//@ aux-build: ../../../lib/proc_macro/src/lib.rs
//@ compile-variant: -O

extern crate lib as source_proc_macro;

fn main() {
    for invalid in ["\\", "`", "\\word", "`word"] {
        assert!(invalid.parse::<source_proc_macro::TokenStream>().is_err());
    }
    for empty in ["", " \t\n", "/**/", "// empty\n"] {
        let stream = empty.parse::<source_proc_macro::TokenStream>().unwrap();
        assert!(stream.is_empty());
    }
}
