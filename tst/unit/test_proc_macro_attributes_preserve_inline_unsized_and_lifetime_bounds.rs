//@ proc-macro-aux-build: type_phase_identity.rs
extern crate type_phase_identity;
#[type_phase_identity::passthrough]
struct Views<'long: 'short, 'short, T: ?Sized + 'long = str> {
    first: &'long T,
    second: &'short T,
}
#[type_phase_identity::passthrough]
fn retain<'a, T: ?Sized + 'a>(value: &'a T) -> &'a T { value }
fn main() {
    let views = Views { first: "alpha", second: "beta" };
    assert_eq!(retain(views.first), "alpha");
    assert_eq!(retain(views.second), "beta");
}
