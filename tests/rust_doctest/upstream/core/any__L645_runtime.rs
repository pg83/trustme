// Extracted from library/core/src/any.rs:645
#![allow(unused)]
fn main() {
    type SubType = fn(&());
    type SuperType = fn(&'static ());
    type CoVar<T> = Vec<T>; // imagine something more complicated
    
    let sub: CoVar<SubType> = CoVar::new();
    // we have a `CoVar<SuperType>` instance without
    // *ever* having called `CoVar::<SuperType>::new()`!
    let fake_super: CoVar<SuperType> = sub;
}
