//@ compile-fail: Drop can only be implemented for structs, enums, and unions
#![feature(inherent_associated_types)]
#![allow(incomplete_features)]
use std::sync::atomic::{AtomicU8, Ordering};
static COUNT: AtomicU8 = AtomicU8::new(0);
struct Payload;
struct Family;
impl Family { type Item = Payload; }
impl Drop for Family::Item { fn drop(&mut self) { COUNT.fetch_add(1, Ordering::SeqCst); } }
fn main() { drop(Payload); assert_eq!(COUNT.load(Ordering::SeqCst), 1); }
