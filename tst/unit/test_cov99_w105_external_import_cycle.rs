//@ edition: 2021
//@ aux-build: cov99_w105_import_cycle_aux.rs

use cov99_w105_import_cycle_aux::{Exported, Kind, Variant};

fn main() {
    assert_eq!(Exported(), 105);
    let _: Kind = Variant;
}
