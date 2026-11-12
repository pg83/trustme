#![feature(specialization)]

trait SameKind<Rhs> {
    fn same_kind(&self, other: &Rhs) -> bool;
}

impl<Left, Right> SameKind<Right> for Left {
    default fn same_kind(&self, _: &Right) -> bool {
        false
    }
}

impl<Value> SameKind<Value> for Value {
    fn same_kind(&self, _: &Value) -> bool {
        true
    }
}

fn main() {
    let first = || 17u8;
    let second = || 17u8;
    assert!(first.same_kind(&first));
    assert!(!first.same_kind(&second));
    assert_eq!(first(), second());

    let captured = 29u8;
    let third = move || captured;
    assert!(third.same_kind(&third));
    assert!(!third.same_kind(&first));
    assert_eq!(third(), 29);
}
