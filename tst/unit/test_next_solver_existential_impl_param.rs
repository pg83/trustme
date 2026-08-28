//@ crate-type: lib
//@ compile-flags: -Znext-solver=globally

#![feature(lang_items, no_core)]
#![no_core]

#[lang = "pointee_sized"]
trait PointeeSized {}

#[lang = "meta_sized"]
trait MetaSized: PointeeSized {}

#[lang = "sized"]
trait Sized: MetaSized {}

// `U` and `N` do not occur in the impl head. They are real candidate
// existentials: the nested associated equality determines both after head
// assembly has already rolled its inference transaction back.
trait Source {
    type Item;
}

struct S;
struct Pair<T, const N: usize>([T; N]);

impl Source for S {
    type Item = Pair<u32, 7>;
}

trait Make {
    type Output;
}

impl<U, const N: usize> Make for S
where
    S: Source<Item = Pair<U, N>>,
{
    type Output = Pair<U, N>;
}

fn require_output(_: <S as Make>::Output) {}

fn probe(value: Pair<u32, 7>) {
    require_output(value);
}
