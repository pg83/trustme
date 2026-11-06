use std::fmt::Display;
trait Store {
    type Value: ?Sized;
    fn value(&self) -> &Self::Value;
}
struct Text;
impl Store for Text {
    type Value = str;
    fn value(&self) -> &str { "nested opaque" }
}
fn make<'a>() -> Box<dyn Store<Value = impl ?Sized + Display + 'a + use<'a>> + 'a> {
    Box::new(Text)
}
fn main() {
    assert_eq!(make().value().to_string(), "nested opaque");
}
