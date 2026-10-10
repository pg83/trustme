//@ aux-build: approx_ulps_struct.rs
// cgmath 0.18's `ulps_eq!(Self::dot(self, other), &Self::Scalar::zero())`
// under `where Self::Scalar: UlpsEq`: the receiver `Ulps<?A, ?B>` comes from
// `Ulps::default()`, the first argument fixes `A`, and before the second is
// coerced the obligation `A: UlpsEq<?B>` has only the where-clause, so
// `B = A` and `&&Scalar` dereferences to `&Scalar` (rustc resolves pending
// obligations before coercing each argument). The impl's own parameters,
// open in the receiver's type, are decided as the method's are.
#[macro_use]
extern crate approx_ulps_struct;
pub trait Space: Copy {
    type Scalar: Copy + Default;
    fn dot(self, other: Self) -> Self::Scalar;

    fn is_perpendicular(self, other: Self) -> bool
    where
        Self::Scalar: approx_ulps_struct::UlpsEq,
    {
        ulps_eq!(Self::dot(self, other), &Self::Scalar::default())
    }
}

#[derive(Clone, Copy)]
struct V(f32, f32);

impl Space for V {
    type Scalar = f32;
    fn dot(self, other: V) -> f32 {
        self.0 * other.0 + self.1 * other.1
    }
}

fn main() {
    assert!(V(1.0, 0.0).is_perpendicular(V(0.0, 1.0)));
    assert!(!V(1.0, 0.0).is_perpendicular(V(1.0, 1.0)));
}
