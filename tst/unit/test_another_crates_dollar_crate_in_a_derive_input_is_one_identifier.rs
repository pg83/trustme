//@ aux-build: dollar_crate_item.rs
//@ proc-macro-aux-build: field_getter_derive.rs
// ron puts `#[derive(Serialize)]` on a `bitflags!` struct whose field is
// `<.. as $crate::__private::PublicFlags>::Internal`. The derive gets the item
// with `$crate` as the one identifier upstream passes, and a `$crate` it gives
// back names the macro's crate again.
use dollar_crate_item::{wrapped, Inner};
use field_getter_derive::FieldGetter;

wrapped! {
    #[derive(FieldGetter)]
    Foo
}

fn main() {
    assert_eq!(Foo(Inner(5)).get().0, 5);
}
