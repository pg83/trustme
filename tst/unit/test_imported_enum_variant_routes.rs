//@ aux-build: enum_import_routes.rs
extern crate enum_import_routes as routes;
use routes::shapes::Record::{Empty, Pair, Named};
use routes::Alias::{Empty as AliasEmpty, Pair as AliasPair, Named as AliasNamed};
use routes::shapes::Flag::{Off, On};
use routes::FlagAlias::On as AliasOn;
use routes::Alias as LocalAlias;
use LocalAlias::Pair as LocalPair;
fn value(record: routes::Alias) -> u8 {
    match record { Empty => 0, Pair(a, b) => a + b, Named { value } => value }
}
fn main() {
    assert_eq!(value(AliasEmpty), 0);
    assert_eq!(value(AliasPair(3, 5)), 8);
    assert_eq!(value(AliasNamed { value: 11 }), 11);
    assert_eq!(value(LocalPair(13, 17)), 30);
    assert_eq!(Off as u8, 3);
    assert_eq!(On as u8, AliasOn as u8);
}
