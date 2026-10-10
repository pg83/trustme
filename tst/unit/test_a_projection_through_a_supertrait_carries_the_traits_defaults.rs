// cgmath (spade's dependency) writes `type Epsilon = S::Epsilon` with
// `S: BaseFloat` and `BaseFloat: approx::AbsDiffEq<Epsilon = Self>`, where
// `AbsDiffEq<Rhs = Self>`. Upstream lowers every trait reference with its
// defaulted arguments, so the path is `<S as AbsDiffEq<S>>::Epsilon`. We
// filled the defaults only on the way to a supertrait, not for the trait
// that names the item, and left `<S as AbsDiffEq>::Epsilon` for a later
// visit to complete. The impls are checked in path order: here `UlpsEq`'s
// impl came first, its `Self::Epsilon` reached the unvisited
// `<S as AbsDiffEq>::Epsilon`, which matched no bound, and the method's
// `S::Epsilon` (already `S`) was reported as a mismatch.
pub trait UlpsEq<Rhs = Self>: AbsDiffEq<Rhs>
where
    Rhs: ?Sized,
{
    fn ulps_eq(&self, other: &Rhs, epsilon: Self::Epsilon, max_ulps: u32) -> bool;
}

pub trait AbsDiffEq<Rhs = Self>: PartialEq<Rhs>
where
    Rhs: ?Sized,
{
    type Epsilon;
    fn abs_diff_eq(&self, other: &Rhs, epsilon: Self::Epsilon) -> bool;
}

impl AbsDiffEq for f32 {
    type Epsilon = f32;
    fn abs_diff_eq(&self, other: &f32, epsilon: f32) -> bool {
        (self - other).abs() <= epsilon
    }
}

impl UlpsEq for f32 {
    fn ulps_eq(&self, other: &f32, epsilon: f32, _max_ulps: u32) -> bool {
        (self - other).abs() <= epsilon
    }
}

pub trait BaseFloat: Copy + AbsDiffEq<Epsilon = Self> + UlpsEq<Epsilon = Self> {}

impl<T: Copy + AbsDiffEq<Epsilon = T> + UlpsEq<Epsilon = T>> BaseFloat for T {}

#[derive(PartialEq, Clone, Copy)]
pub struct Pair<S> {
    x: S,
    y: S,
}

impl<S: BaseFloat> UlpsEq for Pair<S> {
    fn ulps_eq(&self, other: &Self, epsilon: S::Epsilon, max_ulps: u32) -> bool {
        self.x.ulps_eq(&other.x, epsilon, max_ulps) && self.y.ulps_eq(&other.y, epsilon, max_ulps)
    }
}

impl<S: BaseFloat> AbsDiffEq for Pair<S> {
    type Epsilon = S::Epsilon;
    fn abs_diff_eq(&self, other: &Self, epsilon: S::Epsilon) -> bool {
        self.x.abs_diff_eq(&other.x, epsilon) && self.y.abs_diff_eq(&other.y, epsilon)
    }
}

fn main() {
    let p = Pair { x: 1.0f32, y: 2.0 };
    assert!(p.ulps_eq(&p, f32::EPSILON, 4));
    assert!(p.abs_diff_eq(&Pair { x: 1.0, y: 2.0 }, f32::EPSILON));
}
