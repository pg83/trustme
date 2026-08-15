trait Variable<'a> {
    type Type;
}

impl Variable<'_> for () {
    type Type = ();
}

fn check<F, T>(_: F)
where
    F: Fn(T),
    F: for<'a> Fn(<T as Variable<'a>>::Type),
    T: for<'a> Variable<'a>,
{
}

fn takes_unit(_: ()) {}

fn test(argument: impl Fn(())) {
    let inferred_closure = |value| takes_unit(value);

    check(takes_unit);
    check(inferred_closure);
    check(argument);
}

fn main() {}
