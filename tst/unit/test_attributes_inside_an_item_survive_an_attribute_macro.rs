//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// hyper's `#[tokio::test] async fn parse_reads_until_blocked` builds a
// `ParseContext { .., #[cfg(feature = "ffi")] preserve_header_order: false, .. }`.
// An attribute macro receives its item's tokens, attributes inside the body
// included, and the `cfg` is evaluated in what it returns. The item is sent
// here as its pretty-printed text, and the printer (a port of upstream's
// `rustc_ast_pretty`) printed no attributes: the field came back without its
// `cfg`, and "Field 'preserve_header_order' not found". Upstream prints the
// outer attributes of a struct expression's fields, of match arms, of `let`
// statements and of expressions, which `stringify!` shows as well.
extern crate proc_macro_item_passthrough;

use proc_macro_item_passthrough::echo_item;

macro_rules! show {
    ($e:expr) => {
        stringify!($e)
    };
}

struct Ctx {
    first: u32,
    #[cfg(any())]
    hidden: u32,
    last: u32,
}

#[echo_item]
fn build(base: u32) -> u32 {
    let ctx = Ctx {
        first: base,
        #[cfg(any())]
        hidden: 7,
        last: base + 1,
    };
    #[allow(unused_variables)]
    let unused = 0;
    match ctx.first {
        #[cfg(any())]
        1 => 100,
        _ => ctx.first + ctx.last,
    }
}

fn main() {
    assert_eq!(build(1), 3);
    assert_eq!(show!(Ctx { first: 1, #[cfg(any())] hidden: 2, last: 3 }), "Ctx { first: 1, #[cfg(any())] hidden: 2, last: 3 }");
    assert_eq!(show!(match 1 { #[allow(unused)] 1 => 2, _ => 3 }), "match 1 { #[allow(unused)] 1 => 2, _ => 3 }");
    assert_eq!(show!({ #[allow(unused)] let x = 1; x }), "{ #[allow(unused)] let x = 1; x }");
    assert_eq!(show!(#[allow(unused)] 5), "#[allow(unused)] 5");
    assert_eq!(show!(Ctx { #[doc = "x"] first: 1, last: 3 }), "Ctx { #[doc = \"x\"] first: 1, last: 3 }");
}
