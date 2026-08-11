use std::ops::Add;

#[derive(Clone, Copy)]
struct Number(u32);

impl Add for Number {
    type Output = Number;
    fn add(self, other: Number) -> Number { Number(self.0 + other.0) }
}

impl Add<Number> for &Number {
    type Output = Number;
    fn add(self, other: Number) -> Number { (*self).add(other) }
}
