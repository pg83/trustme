//@ proc-macro-aux-build: trait_attribute_edits.rs
extern crate trait_attribute_edits as edits;

#[edits::passthrough]
trait Text<'a> {
    type Body: ?Sized + 'a;
    fn text(&'a self) -> &'a Self::Body;
}

struct Value(&'static str);
impl<'a> Text<'a> for Value {
    type Body = str;
    fn text(&'a self) -> &'a str { self.0 }
}

fn main() { assert_eq!(Value("trait").text(), "trait"); }
