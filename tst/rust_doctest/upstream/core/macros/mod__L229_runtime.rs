// Extracted from library/core/src/macros/mod.rs:229
#![allow(unused)]
#![feature(cfg_select)]
fn main() {

    let _some_string = cfg_select! {
        unix => { "With great power comes great electricity bills" }
        _ => { "Behind every successful diet is an unwatched pizza" }
    };
}
