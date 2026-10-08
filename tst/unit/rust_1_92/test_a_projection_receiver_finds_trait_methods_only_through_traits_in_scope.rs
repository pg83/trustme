// elliptic-curve's `ScalarValue::is_zero` is `self.inner.is_zero().into()`
// with `inner: C::Uint`, whose bounds reach crypto-bigint's `Zero` and,
// through `ConstZero`, num-traits' `Zero`; only crypto-bigint's is
// imported. rustc's method probe takes inherent candidates from a type
// parameter's bounds (`assemble_inherent_candidates_from_param`) but none
// from an alias: on a projection a trait method is found only through a
// trait in scope. We also offered the methods of every trait the
// associated type's bounds reach, and the two `is_zero`s were ambiguous.
mod nt {
    pub trait Zero {
        fn is_zero(&self) -> bool;
    }
    pub trait ConstZero: Zero {
        const ZERO: Self;
    }
}

mod cb {
    pub trait Zero {
        fn is_zero(&self) -> u8;
    }
    pub trait Constants: crate::nt::ConstZero {}
    pub trait FixedInteger: Constants + Zero {}
}

use cb::Zero;
use nt::ConstZero;

trait Curve {
    type Uint: cb::FixedInteger;
}

fn is_zero<C: Curve>(x: &C::Uint) -> u8 {
    x.is_zero().into()
}

struct U(u8);
impl nt::Zero for U {
    fn is_zero(&self) -> bool {
        self.0 == 0
    }
}
impl nt::ConstZero for U {
    const ZERO: U = U(0);
}
impl cb::Zero for U {
    fn is_zero(&self) -> u8 {
        (self.0 == 0) as u8 + 6
    }
}
impl cb::Constants for U {}
impl cb::FixedInteger for U {}

struct K;
impl Curve for K {
    type Uint = U;
}

fn main() {
    assert_eq!(is_zero::<K>(&U::ZERO), 7);
    assert_eq!(is_zero::<K>(&U(3)), 6);
    assert!(nt::Zero::is_zero(&U::ZERO));
}
