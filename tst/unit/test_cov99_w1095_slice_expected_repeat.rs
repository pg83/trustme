//@ edition: 2021
//@ compile-fail: Type mismatch

fn main() {
    let _: [u16] = [7; 3];
}
