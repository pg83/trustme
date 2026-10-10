// cgmath's `impl<S, R, E: BaseFloat> approx::UlpsEq for Decomposed<S, R>
// where S: approx::UlpsEq<Epsilon = E>`, with `UlpsEq<Rhs = Self>:
// AbsDiffEq<Rhs>`. Upstream the clause is `<S as UlpsEq<S>>` and its
// binding projects `<S as AbsDiffEq<S>>::Epsilon`. We looked the binding's
// trait up before the bounded type was known, so the default `Rhs = Self`
// stayed a bare `Self` - `AbsDiffEq<Self>` - and inside the impl that
// `Self` was read as `Decomposed<S, R>`. `S: AbsDiffEq<S>` then had no
// `Epsilon` binding, the `AbsDiffEq` impl's `E` was left undetermined, and
// `ulps_eq`'s `epsilon: E` was reported as not matching the trait.
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

#[derive(PartialEq, Clone, Copy)]
pub struct Decomposed<V, R> {
    pub disp: V,
    pub rot: R,
}

impl<S, R, E: Copy> UlpsEq for Decomposed<S, R>
where
    S: UlpsEq<Epsilon = E>,
    R: UlpsEq<Epsilon = E>,
{
    fn ulps_eq(&self, other: &Self, epsilon: E, max_ulps: u32) -> bool {
        R::ulps_eq(&self.rot, &other.rot, epsilon, max_ulps) && S::ulps_eq(&self.disp, &other.disp, epsilon, max_ulps)
    }
}

impl<S, R, E: Copy> AbsDiffEq for Decomposed<S, R>
where
    S: AbsDiffEq<Epsilon = E>,
    R: AbsDiffEq<Epsilon = E>,
{
    type Epsilon = E;
    fn abs_diff_eq(&self, other: &Self, epsilon: E) -> bool {
        R::abs_diff_eq(&self.rot, &other.rot, epsilon) && S::abs_diff_eq(&self.disp, &other.disp, epsilon)
    }
}

fn main() {
    let d = Decomposed { disp: 1.0f32, rot: 2.0f32 };
    assert!(d.ulps_eq(&d, f32::EPSILON, 4));
    assert!(d.abs_diff_eq(&d, f32::EPSILON));
}
