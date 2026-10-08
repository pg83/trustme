//@ aux-build: lane_constants.rs
// keccak's `impl LaneSize for u8 { const RC: &[Self] = &{ .. }; }` is used
// only from the generic `keccak_p::<L>`, so the crate that instantiates it
// links against the allocation the constant's value points to. Evaluating a
// constant made that allocation an ordinary private static of the defining
// crate, emitted only when the crate's own code used it, and the program
// failed to link ("undefined symbol"). An allocation that a constant's
// evaluation produces is promoted, as the lifted borrows of other constants
// are, and its crate always emits it.
fn main() {
    assert_eq!(lane_constants::sum::<u8>(), 6);
}
