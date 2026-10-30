//@ aux-build: enum_alias_routes.rs
use enum_alias_routes::facade::Message::{Empty as Nothing, Pair as Both, Record as Row};
use enum_alias_routes::facade::Status::{Ready as Start, Busy as Wait};
fn inspect(value: enum_alias_routes::Event) -> u16 {
    match value { Nothing => 0, Both(left, right) => left as u16 + right, Row { count } => count * 2 }
}
fn main() {
    assert_eq!(inspect(Nothing), 0);
    assert_eq!(inspect(Both(5, 17)), 22);
    assert_eq!(inspect(Row { count: 19 }), 38);
    assert_eq!(Start as u8 + Wait as u8, 20);
}
