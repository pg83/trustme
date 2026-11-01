//@ aux-build: normalised_import_base.rs
//@ aux-build: normalised_import_middle.rs
use normalised_import_middle::{Alias, calculate, generate, SUM, VALUE};
use normalised_import_middle::{NamedAlias, TupleAlias, UnitAlias};

fn main() {
    let value: Alias = Alias(calculate(7));
    assert_eq!(generate!(value.0) + SUM + VALUE, 47);
    assert!(matches!(UnitAlias, normalised_import_base::inside::Choice::Unit));
    assert!(matches!(TupleAlias(23), normalised_import_base::inside::Choice::Tuple(23)));
    assert!(matches!(NamedAlias { value: 29 }, normalised_import_base::inside::Choice::Named { value: 29 }));
}
