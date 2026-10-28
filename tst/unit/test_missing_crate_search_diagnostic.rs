//@ compile-fail: Unable to locate crate 'missing_coverage_dependency' in search directories
extern crate missing_coverage_dependency;
fn main() { let _ = missing_coverage_dependency::VALUE; }
