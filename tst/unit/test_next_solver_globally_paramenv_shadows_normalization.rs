//@ crate-type: lib
//@ compile-flags: -Znext-solver=globally

// rustc candidate preference: normalisation does not consider impls when the
// trait goal is proven via a non-global ParamEnv candidate.  Inside the
// TryInto2 blanket the bare `U: TryFrom2<T>` bound keeps `U::Error` rigid;
// it must NOT normalise to `Never` through the reverse blanket
// `impl TryFrom2<U> for T where U: Into2<T>`.  Mirrors the libcore
// TryFrom/TryInto blanket pair (convert/mod.rs); verified against real
// rustc -Znext-solver.

enum Never {}

trait Into2<T> {
    fn into2(self) -> T;
}

trait TryFrom2<T>: Sized {
    type Error;
    fn try_from2(v: T) -> Result<Self, Self::Error>;
}

trait TryInto2<T> {
    type Error;
    fn try_into2(self) -> Result<T, Self::Error>;
}

impl<T, U> TryInto2<U> for T
where
    U: TryFrom2<T>,
{
    type Error = U::Error;
    fn try_into2(self) -> Result<U, U::Error> {
        U::try_from2(self)
    }
}

impl<T, U> TryFrom2<U> for T
where
    U: Into2<T>,
{
    type Error = Never;
    fn try_from2(v: U) -> Result<Self, Never> {
        Ok(v.into2())
    }
}
