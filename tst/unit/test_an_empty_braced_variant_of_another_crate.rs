//@ aux-build: empty_variant_braces_aux.rs
// sqlparser's tests build `Statement::Set(Set::SetNamesDefault {})`, an empty
// struct variant of the sqlparser crate. Upstream checks a struct expression
// against the variant's fields wherever the enum is defined: no fields, no
// values, for a struct variant, a tuple variant with no fields or a unit one.
extern crate empty_variant_braces_aux as aux;
use aux::Set;

fn main() {
    assert_eq!(Set::SetNamesDefault {}, Set::SetNamesDefault {});
    assert!(matches!(Set::SetNamesDefault {}, Set::SetNamesDefault {}));
    assert_eq!(Set::Tuple {}, Set::Tuple());
    assert_eq!(Set::Unit {}, Set::Unit);
    assert_eq!(Set::Named { a: 1 }, Set::Named { a: 1 });
}
