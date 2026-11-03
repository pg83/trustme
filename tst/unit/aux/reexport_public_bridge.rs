pub extern crate reexport_seed;
pub use reexport_seed as origin;
pub use reexport_seed::doorway as facade;
pub mod nested {
    pub use reexport_seed::first as values;
    pub use reexport_seed::doorway::second::Choice;
}
