//@ edition: 2024

#![feature(mut_ref, ref_pat_eat_one_layer_2024)]
#![allow(incomplete_features)]

struct Foo(u8);

fn main() {
    let Foo(mut shared) = &Foo(0);
    shared = &42;
    assert_eq!(*shared, 42);

    let Foo(mut unique) = &mut Foo(0);
    let mut replacement = 42;
    unique = &mut replacement;
    assert_eq!(*unique, 42);
}
