#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

trait Foo<const N: usize> {
    type Assoc: Default;
}

impl Foo<0> for () {
    type Assoc = u32;
}

impl Foo<3> for () {
    type Assoc = i64;
}

fn projected<T, const N: usize>(_: T) -> <() as Foo<{ N + 1 }>>::Assoc
where
    (): Foo<{ N + 1 }>,
{
    Default::default()
}

fn main() {
    let mut value = Default::default();
    value = projected::<_, 2>(value);
}
