//@ compile-fail: Unable to find
fn attempt<const IMPORT_COUNT: usize>() {
    use IMPORT_COUNT as SAVED;
    let _: [u8; SAVED] = [];
}
fn main() { attempt::<0>(); }
