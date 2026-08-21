//@ crate-type: lib
//@ compile-flags: -Znext-solver

trait Constrain<T> {
    type Assoc;
}

impl<T> Constrain<T> for () {
    type Assoc = ();
}

struct Foo<T, U = <() as Constrain<T>>::Assoc>(T, U);

impl<T: Copy> Foo<T> {
    fn select() {}
}

struct Other;

impl Foo<Other> {
    fn select() {}
}

type Alias<T> = Foo<T>;

fn through_wf_guidance<T: Copy>()
where
    (): Constrain<T>,
{
    Alias::select();
}
