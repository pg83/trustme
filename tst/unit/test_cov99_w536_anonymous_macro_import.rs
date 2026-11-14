//@ crate-type: lib

#[macro_export]
macro_rules! coverage_marker {
    () => {};
}

mod anonymous_import {
    use crate::coverage_marker as _;
}
