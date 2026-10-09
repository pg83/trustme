// approx 0.5's traits: `RelativeEq<Rhs = Self>: AbsDiffEq<Rhs>`, `Epsilon` declared
// by the supertrait.
pub trait AbsDiffEq<Rhs = Self>: PartialEq<Rhs>
where
    Rhs: ?Sized,
{
    type Epsilon;
    fn default_epsilon() -> Self::Epsilon;
}

impl AbsDiffEq for f32 {
    type Epsilon = f32;
    fn default_epsilon() -> f32 {
        0.5
    }
}

pub trait RelativeEq<Rhs = Self>: AbsDiffEq<Rhs>
where
    Rhs: ?Sized,
{
    fn default_max_relative() -> Self::Epsilon;
}

impl RelativeEq for f32 {
    fn default_max_relative() -> f32 {
        0.25
    }
}
