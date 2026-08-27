//@ crate-type: lib
//@ edition: 2024

fn requires_eq<T: Eq>() {}

pub fn check() {
    requires_eq::<[*const (); 2]>();
}
