// A proc macro's literal comes back to the compiler as its text and is lexed
// on its own; a float that is the last thing in that text is still a float.
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

use proc_macro_item_passthrough::literal_from_text;

fn main() {
    assert_eq!(literal_from_text!("2.5"), 2.5);
    assert_eq!(literal_from_text!("7"), 7);
}
