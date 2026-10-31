//@ aux-build: cli_trait_fixture.rs
//@ aux-search: cli_trait_fixture.rs
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs
use cli_trait_fixture::{evaluate, Evaluate, Record};
use proc_macro_item_passthrough::echo;
struct Values(Record<3>);
echo! {
    impl Evaluate for Values {
        fn evaluate(&self) -> u16 { self.0.0.iter().map(|value| *value as u16).sum() }
    }
}
fn main() { let values = Values(Record([2, 3, 5])); assert_eq!(evaluate(&values), 17); }
