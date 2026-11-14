//@ edition: 2021
//@ compile-only

#[derive(PartialEq, PartialOrd)]
struct Mark<const N: usize>;

fn main() {
    Mark::<0>>=Mark::<0>;
}
