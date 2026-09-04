// A where-clause can claim a type is Sized when it is not - that is the point of
// the feature, and the body is checked under the claim. What the claim cannot do
// is change what the type is: `T<dyn A>` still ends in a trait object, so
// reaching it from `T<i32>` is still an unsizing. Treating "provably Sized" as
// "cannot be unsized into" turned the cast into an equality and the two sides
// then did not match.

#![feature(trivial_bounds)]
#![allow(unused)]

trait A {}

impl A for i32 {}

struct T<X: ?Sized> {
    x: X,
}

fn unsized_local()
where
    for<'a> T<dyn A + 'a>: Sized,
{
    let x: T<dyn A> = *(Box::new(T { x: 1 }) as Box<T<dyn A>>);
}

fn main() {}
