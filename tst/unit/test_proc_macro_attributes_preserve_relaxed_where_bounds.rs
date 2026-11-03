//@ proc-macro-aux-build: type_phase_identity.rs
extern crate type_phase_identity;
#[type_phase_identity::passthrough]
fn length<'a, T>(value: &'a T) -> usize
where T: ?Sized + AsRef<str> + 'a {
    value.as_ref().len()
}
#[type_phase_identity::passthrough]
struct View<'a, T> where T: ?Sized + 'a { value: &'a T }
fn main() {
    let view = View { value: "boundaries" };
    assert_eq!(length(view.value), 10);
    assert_eq!(length(&String::from("where")), 5);
}
