//@ compile-fail: Unexpected attribute path
macro_rules! attribute { ($path:path) => { #[$path] fn main() {} }; }
attribute!(::custom);
