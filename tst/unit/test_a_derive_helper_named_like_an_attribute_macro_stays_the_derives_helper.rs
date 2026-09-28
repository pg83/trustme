//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// derive-where's `#[derive_where(Clone)]` gives back
// `#[derive(DeriveWhere)] #[derive_where(Clone)] #[derive_where_visited] item`,
// and `DeriveWhere` declares `attributes(derive_where)`. Upstream a derive's
// helpers are in scope for the item it is applied to and for what later
// attribute macros on that item give back (`Scope::DeriveHelpers`, searched
// before any macro scope), so after `derive_where_visited` hands the item back,
// its `#[derive_where(Clone)]` is still the helper. Here it was resolved as the
// attribute macro again, which gave back the same item, and so on for ever.
use proc_macro_item_passthrough::{helper_forward, helper_forward_visited, HelperForward};

#[helper_forward(x)]
struct Tagged;

#[helper_forward(y)]
enum Picked {
    A,
}

fn main() {
    assert_eq!(Tagged::helper_forward_seen(), 1);
    assert_eq!(Picked::helper_forward_seen(), 1);
    let _ = Picked::A;
}
