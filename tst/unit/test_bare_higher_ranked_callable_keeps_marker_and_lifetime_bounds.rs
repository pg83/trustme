//@ edition: 2015
#![allow(bare_trait_objects)]
type Callback = for<'a> Fn(&'a u16) -> u16 + for<'b> Send + Sync + 'static;
fn twice(value: &u16) -> u16 { *value * 2 }
fn call(callback: &Callback, value: u16) -> u16 { callback(&value) }
fn main() {
    let callback: &Callback = &twice;
    assert_eq!(call(callback, 13), 26);
}
