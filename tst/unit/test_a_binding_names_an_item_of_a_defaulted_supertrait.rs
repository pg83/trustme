//@ aux-build: approx_like_traits.rs
// cgmath 0.18: `trait BaseFloat: .. + approx::RelativeEq<Epsilon = Self>`.
// `Epsilon` is declared by `AbsDiffEq<Rhs>`, the supertrait of
// `RelativeEq<Rhs = Self>`, and the bound leaves `Rhs` to its default; the
// supertrait is found with the default filled in, as in rustc's trait ref.
// The same with traits of this crate.
trait LocalAbsDiffEq<Rhs = Self>: PartialEq<Rhs>
where
    Rhs: ?Sized,
{
    type Epsilon;
    fn local_epsilon() -> Self::Epsilon;
}

impl LocalAbsDiffEq for f64 {
    type Epsilon = f64;
    fn local_epsilon() -> f64 {
        0.125
    }
}

trait LocalRelativeEq<Rhs = Self>: LocalAbsDiffEq<Rhs>
where
    Rhs: ?Sized,
{
}

impl LocalRelativeEq for f64 {}

trait BaseFloat: Copy + approx_like_traits::AbsDiffEq<Epsilon = Self> + approx_like_traits::RelativeEq<Epsilon = Self> {}

impl<T> BaseFloat for T where T: Copy + approx_like_traits::AbsDiffEq<Epsilon = Self> + approx_like_traits::RelativeEq<Epsilon = Self> {}

trait LocalFloat: Copy + LocalRelativeEq<Epsilon = Self> {}

impl<T> LocalFloat for T where T: Copy + LocalRelativeEq<Epsilon = Self> {}

fn eps_of<F: BaseFloat>(_x: F) -> (F, F) {
    (F::default_epsilon(), F::default_max_relative())
}

fn local_eps_of<F: LocalFloat>(_x: F) -> F {
    F::local_epsilon()
}

fn main() {
    assert_eq!(eps_of(1.0f32), (0.5, 0.25));
    assert_eq!(local_eps_of(1.0f64), 0.125);
}
