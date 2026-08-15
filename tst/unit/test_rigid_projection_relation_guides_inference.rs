trait Foo {
    type Assoc<'a, 'b>: 'static;
}

struct Wrapper<T>(T);

fn projected<'a, 'b, T: Foo>(_: T::Assoc<'a, 'b>) {}

fn check<'b, T: Foo>() {
    let _: Wrapper<for<'a> fn(T::Assoc<'a, 'b>)> = Wrapper(projected);
}

fn main() {}
