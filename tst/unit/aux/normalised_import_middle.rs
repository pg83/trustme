pub extern crate normalised_import_base as dependency;
pub use dependency as whole;
pub use dependency::inside as module;
pub use dependency::inside::{Record as Alias, add as calculate, TOTAL as SUM, NUMBER as VALUE};
pub use dependency::inside::Choice::{Named as NamedAlias, Tuple as TupleAlias, Unit as UnitAlias};
pub use dependency::token as generate;

pub mod facade {
    pub use super::{Alias as Renamed, NamedAlias, TupleAlias, UnitAlias};
    pub use super::{calculate as evaluate, generate as expression, SUM, VALUE};
}
