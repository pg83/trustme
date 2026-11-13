//@ aux-build: coverage_associated_value_defaults.rs
extern crate coverage_associated_value_defaults as origin;

use origin::Supply;

struct Inherited;
impl Supply for Inherited {}

struct Overridden;
impl Supply for Overridden {
    const VALUE: u16 = 1333;
}

trait Local {
    const VALUE: u16 = 1337;
}

struct LocalDefault;
impl Local for LocalDefault {}

trait Required {
    const VALUE: u16;
}

struct Provided;
impl Required for Provided {
    const VALUE: u16 = 1361;
}

fn read<T: Supply>() -> u16 {
    T::VALUE
}

fn main() {
    assert_eq!(read::<Inherited>(), 1327);
    assert_eq!(read::<Overridden>(), 1333);
    assert_eq!(read::<origin::ExternalOverride>(), 1329);
    assert_eq!(<LocalDefault as Local>::VALUE, 1337);
    assert_eq!(<Provided as Required>::VALUE, 1361);
}
