extern crate builtin_reexport_origin as origin;
pub use origin::feature_gate as enabled;
pub use origin::gates::nested_gate as nested_enabled;
