// approx 0.4's `Ulps<A, B = A>` with `impl<A, B> Ulps<A, B> where A: UlpsEq<B>` and
// the `ulps_eq!` macro that calls `Ulps::default().eq(&lhs, &rhs)`.
pub trait AbsDiffEq<Rhs = Self>: PartialEq<Rhs>
where
    Rhs: ?Sized,
{
    type Epsilon;
    fn default_epsilon() -> Self::Epsilon;
    fn abs_diff_eq(&self, other: &Rhs, epsilon: Self::Epsilon) -> bool;
}

pub trait UlpsEq<Rhs = Self>: AbsDiffEq<Rhs>
where
    Rhs: ?Sized,
{
    fn default_max_ulps() -> u32;
    fn ulps_eq(&self, other: &Rhs, epsilon: Self::Epsilon, max_ulps: u32) -> bool;
}

impl AbsDiffEq for f32 {
    type Epsilon = f32;
    fn default_epsilon() -> f32 {
        0.5
    }
    fn abs_diff_eq(&self, other: &f32, epsilon: f32) -> bool {
        (self - other).abs() <= epsilon
    }
}

impl UlpsEq for f32 {
    fn default_max_ulps() -> u32 {
        4
    }
    fn ulps_eq(&self, other: &f32, epsilon: f32, _max_ulps: u32) -> bool {
        self.abs_diff_eq(other, epsilon)
    }
}

pub struct Ulps<A, B = A>
where
    A: UlpsEq<B> + ?Sized,
    B: ?Sized,
{
    pub epsilon: A::Epsilon,
    pub max_ulps: u32,
}

impl<A, B> Default for Ulps<A, B>
where
    A: UlpsEq<B> + ?Sized,
    B: ?Sized,
{
    fn default() -> Ulps<A, B> {
        Ulps { epsilon: A::default_epsilon(), max_ulps: A::default_max_ulps() }
    }
}

impl<A, B> Ulps<A, B>
where
    A: UlpsEq<B> + ?Sized,
    B: ?Sized,
{
    pub fn eq(self, lhs: &A, rhs: &B) -> bool {
        A::ulps_eq(lhs, rhs, self.epsilon, self.max_ulps)
    }
}

#[macro_export]
macro_rules! ulps_eq {
    ($lhs:expr, $rhs:expr) => {
        $crate::Ulps::default().eq(&$lhs, &$rhs)
    };
}
