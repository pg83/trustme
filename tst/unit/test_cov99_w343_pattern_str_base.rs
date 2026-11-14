//@ crate-type: lib
#![feature(pattern_types, pattern_type_macro)]
#![allow(incomplete_features, internal_features)]

pub type Text = std::pat::pattern_type!(str is !null);
