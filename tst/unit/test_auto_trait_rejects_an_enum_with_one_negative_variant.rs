//@ compile-fail: error:
#![feature(auto_traits, negative_impls)]

auto trait Portable {}
struct Local;
impl !Portable for Local {}
enum Parcel { Empty, Payload(Local), Number(u16) }
fn require<T: Portable>() {}

fn main() { require::<Parcel>(); }
