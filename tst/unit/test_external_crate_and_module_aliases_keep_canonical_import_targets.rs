//@ aux-build: normalised_import_base.rs
//@ aux-build: normalised_import_middle.rs
use normalised_import_middle::whole::inside::{Record as ThroughCrate, add as through_crate};
use normalised_import_middle::module::{Record as ThroughModule, NUMBER};
use normalised_import_middle::whole::token as through_macro;

fn main() {
    assert_eq!(ThroughCrate(through_crate(17)).0, 20);
    assert_eq!(ThroughModule(NUMBER).0, 19);
    assert_eq!(through_macro!(23), 28);
}
