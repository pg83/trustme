// A generic associated type is substituted with the arguments its projection
// gives it. Trait selection dropped them and monomorphised `Self::Bar<T>`
// against an empty parameter list, asserting with "Type param out of range".
//
// Same shapes as the gccrs tests gat2.rs, gat3.rs and issue-4293.rs.
trait Foo {
    type Bar<T>;

    fn foo<T>(self, value: T) -> Self::Bar<T>;
}

impl Foo for i32 {
    type Bar<T> = T;

    fn foo<T>(self, value: T) -> Self::Bar<T> {
        value
    }
}

enum Wrapped<T> {
    Some(T),
    None,
}

trait Wrap {
    type Out<T>;

    fn wrap<T>(self) -> Self::Out<T>;
}

impl Wrap for u8 {
    type Out<T> = Wrapped<T>;

    fn wrap<T>(self) -> Self::Out<T> {
        Wrapped::None
    }
}

fn main() {
    assert_eq!(15i32.foo::<i8>(14i8), 14i8);

    let wrapped: Wrapped<u16> = 1u8.wrap::<u16>();
    assert!(matches!(wrapped, Wrapped::None));
}
