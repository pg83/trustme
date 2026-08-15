//@ crate-type: lib
//@ compile-flags: -Znext-solver

trait Test {
    type Assoc;
}

impl Test for i32 {
    type Assoc = i32;
}

impl Test for String {
    type Assoc = String;
}

struct Invariant<T>(Option<*mut T>);

fn transform<T: Test>(_: Invariant<T>) -> Invariant<T::Assoc> {
    loop {}
}

fn check() {
    let mut value: Invariant<_> = Invariant(None);
    value = transform(value);
    value = Invariant::<i32>(None);
}
