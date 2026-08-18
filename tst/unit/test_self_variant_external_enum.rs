// `Self::Variant` where `Self` is an enum from another crate: only that enum's
// HIR is here, and the variant list is the same either way.
trait ConstDefault {
    const DEFAULT: Self;
}

impl ConstDefault for Option<i32> {
    const DEFAULT: Self = Self::Some(23);
}

impl ConstDefault for Result<i32, ()> {
    const DEFAULT: Self = Self::Ok(7);
}

const fn explicit_qpath() -> Option<usize> {
    <Option<usize>>::Some(23)
}

fn main() {
    assert_eq!(<Option<i32> as ConstDefault>::DEFAULT, Some(23));
    assert_eq!(<Result<i32, ()> as ConstDefault>::DEFAULT, Ok(7));
    assert_eq!(explicit_qpath(), Some(23));
}
