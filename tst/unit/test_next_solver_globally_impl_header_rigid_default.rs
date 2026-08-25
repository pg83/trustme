//@ crate-type: lib
//@ compile-flags: -Znext-solver=globally

// An impl header is a declaration: its projections must stay rigid (like
// struct fields and signatures) instead of being eagerly normalised at
// Resolve UFCS.  A receiver built from the declared form of a defaulted
// parameter (`Foo<T>` = `Foo<T, <() as Constrain<T>>::Assoc>`) then unifies
// with the `impl<T: Copy> Foo<T>` header structurally, alias-vs-alias --
// rustc never normalises impl headers at all.

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

// NOTE: `Foo::<Other>::select()` (both impls' heads apply, bounds must
// disambiguate) is a pre-existing inherent-selection gap unrelated to header
// rigidity: the alias-free equivalent fails identically on master.
fn direct() {
    Foo::<u8>::select();
}
