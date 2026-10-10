//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
// time-macros 0.1 writes `#[proc_macro_hack] pub use time_macros_impl::{date,
// offset, time};`, and proc-macro-hack reads the item as `use`, an identifier,
// `::`, then one name or a braced list of them. Upstream hands an attribute
// macro the item's tokens as written. We kept only the flattened paths of a
// use tree and wrote them back as `use {time_macros_impl::date, ...}`, so the
// macro met a brace where it wanted the crate's name ("expected identifier").
extern crate proc_macro_item_passthrough;

pub mod shapes {
    pub mod round {
        pub fn circle() -> u32 {
            1
        }
        pub fn oval() -> u32 {
            2
        }
    }
    pub fn square() -> u32 {
        4
    }
}

pub mod outer {
    use proc_macro_item_passthrough::echo_item_spelled_compact;

    #[echo_item_spelled_compact("use crate :: shapes :: { round :: { circle , oval as ellipse } , square , self } ;")]
    pub use crate::shapes::{round::{circle, oval as ellipse}, square, self};
}

fn main() {
    assert_eq!(outer::circle() + outer::ellipse() + outer::square() + outer::shapes::square(), 11);
}
