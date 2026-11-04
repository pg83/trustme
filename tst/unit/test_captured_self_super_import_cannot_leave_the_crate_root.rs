//@ compile-fail: Too many `super` components
macro_rules! import_value {
    ($path:path) => { use $path; };
}

import_value!(self::super::super::MISSING);
fn main() {}
