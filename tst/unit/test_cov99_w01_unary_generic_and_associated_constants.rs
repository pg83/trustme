trait Scalars {
    const SIGNED: i32;
    const FLOAT: f64;
}

struct Values;
impl Scalars for Values {
    const SIGNED: i32 = 23;
    const FLOAT: f64 = 3.5;
}

#[inline(never)]
fn associated<T: Scalars>() -> (i32, f64) {
    let signed = T::SIGNED;
    let float = T::FLOAT;
    (-signed, -float)
}

#[inline(never)]
fn generic<const SIGNED: i32, const FLAG: bool>() -> (i32, i32, bool) {
    let signed = SIGNED;
    let flag = FLAG;
    (-signed, !signed, !flag)
}

fn main() {
    assert_eq!(associated::<Values>(), (-23, -3.5));
    assert_eq!(generic::<17, true>(), (-17, -18, false));
}
