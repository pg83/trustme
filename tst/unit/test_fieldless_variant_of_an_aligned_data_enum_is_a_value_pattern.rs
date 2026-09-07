/* crossbeam-utils' `issue_748` test: a `#[repr(align(8))]` data enum with derived `PartialEq` and
   `Debug` - the derives match on `&Test::FieldLess`, the fieldless variant of a data enum. */
#[allow(dead_code)]
#[repr(align(8))]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Test {
    Field(u32),
    FieldLess,
}

fn main() {
    assert_eq!(std::mem::size_of::<Test>(), 8);
    let x = Test::FieldLess;
    assert_eq!(x, Test::FieldLess);
    assert_ne!(Test::Field(1), Test::FieldLess);
}
