// num-modular's `self.powm((n - 1) / 2, &n)` with `n: &u8`: the impls for
// `&u8` differ in `Exp`, so `Modulus` is decided by the one impl that fits
// once the first argument's type is known - and that type is the output of
// two operators still being resolved. rustc selects all pending obligations
// before it coerces the second argument, the operators first, and meets
// `Modulus = &u8`. The second argument now waits for the earlier ones rather
// than reading `Modulus` off itself as `&&u8` ("No applicable methods").
pub trait Pow<E = Self, M = Self> {
    type Output;
    fn powm(self, exp: E, m: M) -> Self::Output;
}
pub trait Symbols<M = Self> {
    fn checked_legendre(&self, n: M) -> Option<i8>;
}
macro_rules! impls {
    ($($T:ty)*) => ($(
        impl Pow<$T, &$T> for $T {
            type Output = $T;
            fn powm(self, exp: $T, m: &$T) -> $T {
                let mut r: u128 = 1;
                let mut i: u128 = 0;
                while i < exp as u128 {
                    r = r * self as u128 % *m as u128;
                    i += 1;
                }
                r as $T
            }
        }
        impl Pow<$T, &$T> for &$T {
            type Output = $T;
            fn powm(self, exp: $T, m: &$T) -> $T {
                (*self).powm(exp, &m)
            }
        }
        impl Pow<&$T, &$T> for $T {
            type Output = $T;
            fn powm(self, exp: &$T, m: &$T) -> $T {
                self.powm(*exp, &m)
            }
        }
        impl Pow<&$T, &$T> for &$T {
            type Output = $T;
            fn powm(self, exp: &$T, m: &$T) -> $T {
                (*self).powm(*exp, &m)
            }
        }
        impl Symbols<&$T> for $T {
            fn checked_legendre(&self, n: &$T) -> Option<i8> {
                match self.powm((n - 1) / 2, &n) {
                    0 => Some(0),
                    1 => Some(1),
                    x if x == n - 1 => Some(-1),
                    _ => None,
                }
            }
        }
    )*);
}
impls!(u8 u16 u32 u64 u128 usize);
fn main() {
    assert_eq!(2u8.checked_legendre(&7), Some(1));
    assert_eq!(3u32.checked_legendre(&7), Some(-1));
    assert_eq!(0usize.checked_legendre(&7), Some(0));
}
