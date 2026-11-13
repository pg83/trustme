//@ crate-type: lib
#![allow(unreachable_code, unused_variables, unused_assignments)]

pub fn unary_destination_invalidation(input: u32) {
    return;
    let destination: u32;
    let source = !input;
    let _observed = !destination;
    destination = source;
}
