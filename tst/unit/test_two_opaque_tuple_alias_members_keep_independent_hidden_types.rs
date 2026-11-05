//@ compile-variant: -O
#![feature(type_alias_impl_trait)]

type Pair = (impl Copy, impl Copy);

#[define_opaque(Pair)]
fn make() -> Pair { (41u8, 43u16) }

fn main() {
    let pair = make();
    assert_eq!(std::mem::size_of_val(&pair.0), 1);
    assert_eq!(std::mem::size_of_val(&pair.1), 2);
    let first_again = pair.0;
    let second_again = pair.1;
    assert_eq!(std::mem::size_of_val(&first_again), 1);
    assert_eq!(std::mem::size_of_val(&second_again), 2);
}
