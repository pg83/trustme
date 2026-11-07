//@ aux-build: external_import_hops.rs
//@ env: TRUSTME_DUMP=hir
#![feature(decl_macro)]

use external_import_hops::entrance::second::inner::{Stamp, CODE};
use external_import_hops::route;
external_import_hops::entrance::bring!(Imported);

fn main() {
    assert_eq!(Stamp(CODE).0, 277);
    assert_eq!(Imported(route::CODE + 2).0, 279);
    assert_eq!(route::Stamp(CODE + 4).0, 281);
}
