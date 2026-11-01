//@ aux-build: normalised_import_base.rs
//@ aux-build: normalised_import_middle.rs
use normalised_import_middle::facade::*;

fn main() {
    assert_eq!(expression!(evaluate(Renamed(11).0)) + SUM, 32);
    assert_eq!(VALUE, 19);
    assert!(matches!(UnitAlias, normalised_import_base::inside::Choice::Unit));
    assert!(matches!(TupleAlias(31), normalised_import_base::inside::Choice::Tuple(31)));
    assert!(matches!(NamedAlias { value: 37 }, normalised_import_base::inside::Choice::Named { value: 37 }));
}
