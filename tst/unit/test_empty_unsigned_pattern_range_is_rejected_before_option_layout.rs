//@ compile-fail: range
#![feature(pattern_types, pattern_type_macro)]
type Window = std::pat::pattern_type!(u8 is ..0);
fn main() {
    let _ = std::mem::size_of::<Option<Window>>();
}
