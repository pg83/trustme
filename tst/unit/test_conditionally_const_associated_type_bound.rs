#![feature(const_trait_impl)]
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

#[const_trait]
trait Trait {
    type Assoc: [const] Trait;

    fn value() -> i32;
}

struct Type<const N: i32>;

fn unqualified<T: const Trait>() -> Type<{ T::Assoc::value() }> {
    Type
}

fn qualified<T: const Trait>() -> Type<{ <T as Trait>::Assoc::value() }> {
    Type
}

fn main() {}
