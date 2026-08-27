//@ crate-type: lib

// The return of `signed` is a nested projection.  Both reductions come from
// associated equalities declared on the bound of `Family::Unsigned`: first
// `Unsigned = Family::Unsigned`, then `Other = Family::Signed`.
trait MinSign {
    type Other;
    type Unsigned: MinSign;
}

trait Sign: MinSign {
    fn signed(self) -> <Self::Unsigned as MinSign>::Other;
}

trait Family {
    type Unsigned: Sign<Other = Self::Signed, Unsigned = Self::Unsigned>;
    type Signed;
}

fn signed<F: Family>(value: F::Unsigned) -> F::Signed {
    value.signed()
}
