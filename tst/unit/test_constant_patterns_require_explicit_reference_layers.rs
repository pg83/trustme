//@ compile-fail: Type mismatch between
#[derive(PartialEq, Eq)]
struct Tag(u16);
const READY: Tag = Tag(7);
fn tag(value: &&Tag) -> bool { matches!(value, READY) }
fn main() {}
