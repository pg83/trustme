//@ proc-macro-aux-build: type_phase_identity.rs
extern crate type_phase_identity;
#[type_phase_identity::passthrough]
trait Source<'a>: 'a {
    type Value: ?Sized + 'a;
    fn get(&'a self) -> &'a Self::Value;
}
struct Message(String);
#[type_phase_identity::passthrough]
impl<'a> Source<'a> for Message {
    type Value = str;
    fn get(&'a self) -> &'a str { &self.0 }
}
fn main() {
    let message = Message(String::from("associated"));
    assert_eq!(message.get(), "associated");
    let source: &dyn Source<'_, Value = str> = &message;
    assert_eq!(source.get(), "associated");
}
