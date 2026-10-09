// `m1.get_unchecked(i) * m2.get_unchecked(j)` on `&[f64]` multiplies two
// `&<?I as SliceIndex<[f64]>>::Output` while the indices are still
// unknown. rustc normalizes such a projection to a fresh inference variable,
// so `&?X: Mul<?R>` matches every `Mul` impl for a reference - core's for
// `&f64` as well as `Ratio`'s `impl<T: Integer> Mul<&T> for &Ratio<T>` - and
// stays ambiguous until the index types are known. Coercing the right-hand
// side into `&f64` is not refuted, only undecided, so it rules nothing out.
// av1-grain's `multiply_mat` is this, with num-rational's `Ratio` in scope.
use std::ops::{Add, Mul};

pub trait Integer: Sized + Clone + Mul<Output = Self> + Add<Output = Self> {}
impl Integer for i32 {}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Ratio<T> {
    numer: T,
    denom: T,
}

impl<T: Clone + Integer> Mul<Ratio<T>> for Ratio<T> {
    type Output = Ratio<T>;
    fn mul(self, rhs: Ratio<T>) -> Ratio<T> {
        Ratio { numer: self.numer * rhs.numer, denom: self.denom * rhs.denom }
    }
}

impl<T: Clone + Integer> Mul<T> for Ratio<T> {
    type Output = Ratio<T>;
    fn mul(self, rhs: T) -> Ratio<T> {
        Ratio { numer: self.numer * rhs, denom: self.denom }
    }
}

impl<'a, 'b, T: Clone + Integer> Mul<&'b Ratio<T>> for &'a Ratio<T> {
    type Output = Ratio<T>;
    fn mul(self, other: &'b Ratio<T>) -> Ratio<T> {
        self.clone() * other.clone()
    }
}

impl<'a, 'b, T: Clone + Integer> Mul<&'b T> for &'a Ratio<T> {
    type Output = Ratio<T>;
    fn mul(self, other: &'b T) -> Ratio<T> {
        self.clone() * other.clone()
    }
}

impl<'a, T: Clone + Integer> Mul<Ratio<T>> for &'a Ratio<T> {
    type Output = Ratio<T>;
    fn mul(self, other: Ratio<T>) -> Ratio<T> {
        self.clone() * other
    }
}

impl<'a, T: Clone + Integer> Mul<T> for &'a Ratio<T> {
    type Output = Ratio<T>;
    fn mul(self, other: T) -> Ratio<T> {
        self.clone() * other
    }
}

impl<'a, T: Clone + Integer> Mul<&'a Ratio<T>> for Ratio<T> {
    type Output = Ratio<T>;
    fn mul(self, other: &Ratio<T>) -> Ratio<T> {
        self * other.clone()
    }
}

impl<'a, T: Clone + Integer> Mul<&'a T> for Ratio<T> {
    type Output = Ratio<T>;
    fn mul(self, other: &T) -> Ratio<T> {
        self * other.clone()
    }
}

fn multiply_mat(m1: &[f64], m2: &[f64], res: &mut [f64], m1_rows: usize, inner_dim: usize, m2_cols: usize) {
    let mut idx = 0;
    for row in 0..m1_rows {
        for col in 0..m2_cols {
            let mut sum = 0f64;
            for inner in 0..inner_dim {
                unsafe {
                    sum += m1.get_unchecked(row * inner_dim + inner)
                        * m2.get_unchecked(inner * m2_cols + col);
                }
            }
            unsafe {
                *res.get_unchecked_mut(idx) = sum;
            }
            idx += 1;
        }
    }
}

fn main() {
    let mut res = [0.0; 1];
    multiply_mat(&[1.0, 2.0], &[3.0, 4.0], &mut res, 1, 2, 1);
    assert_eq!(res[0], 11.0);
    let half = Ratio { numer: 1, denom: 2 };
    assert_eq!(&half * &3, Ratio { numer: 3, denom: 2 });
}
