//@ crate-type: lib
//@ compile-flags: -Znext-solver=globally

// An associated equality on a supertrait may itself name an associated type
// of the trait that introduced the equality.  Normalising that RHS must keep
// its declaring trait instead of attaching the item name to the supertrait.
trait MinInt {
    type OtherSign: MinInt;
    type Unsigned: MinInt;
}

type OtherSign<I> = <I as MinInt>::OtherSign;

trait Int: MinInt {
    fn signed(self) -> OtherSign<Self::Unsigned>;
    fn unsigned(self) -> Self::Unsigned;
    fn wrapping_neg(self) -> Self;
}

trait Float {
    type Int: Int<OtherSign = Self::SignedInt, Unsigned = Self::Int>;
    type SignedInt: Int
        + MinInt<OtherSign = Self::Int, Unsigned = Self::Int>;
}

type IntTy<F> = <F as Float>::Int;

fn round_trip<F: Float>(value: IntTy<F>) -> IntTy<F> {
    value.signed().wrapping_neg().unsigned()
}
