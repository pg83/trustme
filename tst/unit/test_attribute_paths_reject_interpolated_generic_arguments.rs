//@ compile-fail: An attribute path takes no generic arguments
macro_rules! attribute { ($path:path) => { #[$path] fn main() {} }; }
attribute!(custom::<u8>);
