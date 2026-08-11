// Generic type aliases used to ICE in the reachability pass because their
// generic parameters were never resolved into the type context.
#![feature(lang_items)]

pub type Alias<T> = T;
