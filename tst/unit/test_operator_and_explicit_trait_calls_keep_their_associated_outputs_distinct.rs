use std::ops::{Add, AddAssign, Neg, Sub, SubAssign};

#[derive(Clone, Copy, Debug, PartialEq)]
struct Value(i32);

impl Add for Value {
    type Output = Value;
    fn add(self, other: Self) -> Self { Value(self.0 + other.0) }
}
impl Sub for Value {
    type Output = Value;
    fn sub(self, other: Self) -> Self { Value(self.0 - other.0) }
}
impl Neg for Value {
    type Output = Value;
    fn neg(self) -> Self { Value(-self.0) }
}
impl AddAssign for Value {
    fn add_assign(&mut self, other: Self) { self.0 += other.0; }
}
impl SubAssign for Value {
    fn sub_assign(&mut self, other: Self) { self.0 -= other.0; }
}

fn expressions<T>(left: T, right: T) -> (T, T, T)
where T: Copy + Add<Output = T> + Sub<Output = T> + Neg<Output = T> {
    (left + right, left - right, -left)
}

fn assignments<T: Copy + AddAssign + SubAssign>(mut left: T, right: T) -> T {
    left += right;
    SubAssign::sub_assign(&mut left, right);
    AddAssign::add_assign(&mut left, right);
    left -= right;
    left
}

fn main() {
    let (left, right) = (Value(17), Value(4));
    assert_eq!(expressions(left, right), (Value(21), Value(13), Value(-17)));
    assert_eq!(expressions(17i32, 4), (21, 13, -17));
    assert_eq!(Add::add(left, right), left + right);
    assert_eq!(Sub::sub(left, right), left - right);
    assert_eq!(Neg::neg(left), -left);
    assert_eq!(assignments(left, right), left);
    assert_eq!(assignments(17i32, 4), 17);
}
