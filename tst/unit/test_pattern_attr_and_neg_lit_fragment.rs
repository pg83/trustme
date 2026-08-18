// An attribute on a struct-pattern field, and a `$e:expr` fragment holding a
// negative literal used where a pattern takes a value.
struct X {
    foo: i32,
}

macro_rules! enum_number {
    ($name:ident { $($variant:ident = $value:expr, )* }) => {
        enum $name {
            $($variant = $value,)*
        }

        fn from_value(value: i32) -> Option<$name> {
            match value {
                $( $value => Some($name::$variant), )*
                _ => None,
            }
        }
    };
}

enum_number!(Change {
    Down = -1,
    None = 0,
    Up = 1,
});

fn main() {
    let X {
        #[doc(alias = "StructItem")]
        foo,
    } = X { foo: 123 };
    assert_eq!(foo, 123);

    assert!(matches!(from_value(-1), Some(Change::Down)));
    assert!(matches!(from_value(0), Some(Change::None)));
    assert!(matches!(from_value(1), Some(Change::Up)));
    assert!(from_value(7).is_none());
}
