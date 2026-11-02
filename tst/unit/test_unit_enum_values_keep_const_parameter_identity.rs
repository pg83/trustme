//@ compile-variant: -O
#![feature(adt_const_params)]
#![allow(incomplete_features)]
use std::marker::ConstParamTy;
#[derive(ConstParamTy, PartialEq, Eq)]
enum Level { Low, High }
struct Token<const LEVEL: Level>;
fn score<const LEVEL: Level>() -> u8 { match LEVEL { Level::Low => 3, Level::High => 11 } }
fn main() {
    assert_eq!(score::<{ Level::Low }>(), 3);
    assert_eq!(score::<{ Level::High }>(), 11);
    assert_ne!(std::any::TypeId::of::<Token<{ Level::Low }>>(), std::any::TypeId::of::<Token<{ Level::High }>>());
}
