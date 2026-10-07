//@ aux-build: nested_borrowed_consts.rs
// rusqlite 0.39 with time's format_description!: the borrowed array of a const
// block is lifted to a static, and the string literals and items its
// evaluation borrows become further statics. Those were given a path with no
// crate, and the library stopped when it enumerated its statics for codegen.
use nested_borrowed_consts::{time_format, Item, Padding};

fn main() {
    let format = time_format();
    assert_eq!(format.len(), 3);
    assert_eq!(format[0], Item::Component(Padding(2)));
    assert_eq!(format[1], Item::StringLiteral(":"));
    assert_eq!(format[2], Item::Optional(&Item::Compound(&[Item::StringLiteral("."), Item::Component(Padding(2))])));
}
