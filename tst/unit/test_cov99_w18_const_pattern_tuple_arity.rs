//@ compile-fail: Tuple literal node count mismatches with return type

const PAIR: (u8, u8) = (1, 2);

fn main() {
    let PAIR = (1u8,);
}
