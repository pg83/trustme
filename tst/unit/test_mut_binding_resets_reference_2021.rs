struct Foo(u8);

fn main() {
    let Foo(mut value) = &Foo(1);
    value = 42;
    assert_eq!(value, 42);
}
