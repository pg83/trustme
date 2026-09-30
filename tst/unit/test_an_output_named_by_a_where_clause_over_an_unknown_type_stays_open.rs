// serde_with_macros computes `lhs & !rhs` with `lhs: &LazyBool<Cond>`: the impl
// for `&LazyBool<L>` asks `LazyBool<R>: BitAnd<&LazyBool<L>>`, whose impl has
// `L: BitAnd<&R, Output = T>` and `LazyBool<L>: Into<LazyBool<T>>` while `L` is
// still unknown. Upstream leaves `T` a variable with the projection a pending
// goal, and `Into`'s `From<T> for T` makes it `L`. `T` was bound to the alias
// `<L as BitAnd<&R>>::Output` instead, so `Into` asked `L` to be that alias of
// itself, and the right operand's `LazyBool<Cond>` never reached `L`.
use std::ops::{BitAnd, Not};

#[derive(Clone, Debug, PartialEq)]
pub enum LazyBool<T> {
    False,
    Lazy(T),
}

impl<L, R, T> BitAnd<LazyBool<R>> for LazyBool<L>
where
    L: BitAnd<R, Output = T>,
{
    type Output = LazyBool<T>;
    fn bitand(self, _rhs: LazyBool<R>) -> Self::Output {
        LazyBool::False
    }
}

impl<'a, L, R, T> BitAnd<&'a LazyBool<R>> for LazyBool<L>
where
    L: BitAnd<&'a R, Output = T>,
    LazyBool<L>: Into<LazyBool<T>>,
{
    type Output = LazyBool<T>;
    fn bitand(self, rhs: &'a LazyBool<R>) -> Self::Output {
        match (self, rhs) {
            (LazyBool::Lazy(lhs), LazyBool::Lazy(rhs)) => LazyBool::Lazy(lhs & rhs),
            _ => LazyBool::False,
        }
    }
}

impl<'a, L, R, T> BitAnd<LazyBool<R>> for &'a LazyBool<L>
where
    LazyBool<R>: BitAnd<&'a LazyBool<L>, Output = LazyBool<T>>,
{
    type Output = LazyBool<T>;
    fn bitand(self, rhs: LazyBool<R>) -> Self::Output {
        rhs & self
    }
}

impl<T: Not<Output = T>> Not for LazyBool<T> {
    type Output = Self;
    fn not(self) -> Self::Output {
        match self {
            Self::False => Self::False,
            Self::Lazy(this) => Self::Lazy(!this),
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct Cond(pub u32);

impl BitAnd for Cond {
    type Output = Self;
    fn bitand(self, rhs: Self) -> Self::Output {
        Cond(self.0 * 10 + rhs.0)
    }
}

impl BitAnd<&Cond> for Cond {
    type Output = Self;
    fn bitand(self, rhs: &Self) -> Self::Output {
        Cond(self.0 * 10 + rhs.0)
    }
}

impl Not for Cond {
    type Output = Self;
    fn not(self) -> Self::Output {
        Cond(self.0 + 1)
    }
}

fn main() {
    let lhs: &LazyBool<Cond> = &LazyBool::Lazy(Cond(1));
    let rhs: LazyBool<Cond> = LazyBool::Lazy(Cond(2));
    assert_eq!(lhs & !rhs, LazyBool::Lazy(Cond(31)));
}
