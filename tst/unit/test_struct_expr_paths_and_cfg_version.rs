// A struct expression may name a unit or tuple variant, and may name a tuple
// type through an alias, giving its fields by index. `cfg(version(..))` holds
// when the compiler is at least that version.
#![feature(cfg_version)]

enum Examples {
    UnitLike,
    TupleLike(i32),
    StructLike { value: i32 },
}

struct Foo<T>(T, u32);

type TypeAlias = Foo<i32>;

#[cfg(version("1.0"))]
const SINCE_1_0: bool = true;
#[cfg(not(version("99.0")))]
const BEFORE_99: bool = true;

fn main() {
    use Examples::*;

    let unit = UnitLike {};
    let tuple = TupleLike { 0: 123 };
    let named = StructLike { value: 123 };
    assert!(matches!(unit, Examples::UnitLike));
    assert!(matches!(tuple, Examples::TupleLike(123)));
    assert!(matches!(named, Examples::StructLike { value: 123 }));

    let a: Foo<i32> = TypeAlias { 0: 123, 1: 456 };
    assert_eq!((a.0, a.1), (123, 456));
    assert!(SINCE_1_0);
    assert!(BEFORE_99);
    assert!(cfg!(version("1.1")));
    assert!(!cfg!(version("99.100")));
}
