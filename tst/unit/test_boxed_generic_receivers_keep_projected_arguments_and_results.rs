trait Shape { type Value: Copy + core::ops::Add<Output = Self::Value>; }
struct Whole;
impl Shape for Whole { type Value = u32; }
struct Meter<S: Shape>(S::Value);
impl<S: Shape> Meter<S> {
    fn add(self: Box<Self>, other: S::Value) -> S::Value { self.0 + other }
}
fn apply<S: Shape>(meter: Box<Meter<S>>, other: S::Value) -> S::Value {
    meter.add(other)
}
fn main() {
    assert_eq!(apply::<Whole>(Box::new(Meter(19)), 23), 42);
    let inferred = Box::new(Meter::<Whole>(29));
    assert_eq!(inferred.add(31), 60);
}
