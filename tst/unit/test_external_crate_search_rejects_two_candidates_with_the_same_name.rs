//@ aux-build: ambiguous_choice_left/ambiguous_choice.rs
//@ aux-build: ambiguous_choice_right/ambiguous_choice.rs
//@ aux-search: ambiguous_choice
//@ compile-fail: Multiple options for crate 'ambiguous_choice'
extern crate ambiguous_choice;

fn main() {
    let _ = ambiguous_choice::value();
}
