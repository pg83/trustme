// inquire 0.9: `const DEFAULT_DEFAULT_VALUE_FORMATTER: BoolFormatter<'a> =
// &|ans| ..` in an impl, `BoolFormatter` being `&dyn Fn(bool) -> String`.
// The constant's value needs the closure's `Fn` vtable, a static of the
// trait that no impl defines.
type BoolFormatter<'a> = &'a dyn Fn(bool) -> String;

pub const DEFAULT_BOOL_FORMATTER: BoolFormatter = &|ans| {
    if ans {
        String::from("Yes")
    } else {
        String::from("No")
    }
};

struct Confirm;

impl Confirm {
    const DEFAULT_FORMATTER: BoolFormatter<'static> = DEFAULT_BOOL_FORMATTER;
    const DEFAULT_DEFAULT_VALUE_FORMATTER: BoolFormatter<'static> = &|ans| match ans {
        true => String::from("Y/n"),
        false => String::from("y/N"),
    };
}

fn main() {
    assert_eq!((Confirm::DEFAULT_FORMATTER)(true), "Yes");
    assert_eq!((Confirm::DEFAULT_DEFAULT_VALUE_FORMATTER)(false), "y/N");
    assert_eq!(DEFAULT_BOOL_FORMATTER(false), "No");
}
