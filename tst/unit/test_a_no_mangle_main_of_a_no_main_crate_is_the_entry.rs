//@ compile-flags: --test
// icu_locid's examples are `#![no_main]` with `#[no_mangle] fn main(argc, argv)`
// as the program's entry, and `test = true` builds them with `--test`. rustc
// keeps the item's symbol there: `no_main` leaves the harness no entry of its
// own, and the program runs the example. A test harness dropped every
// `#[no_mangle]` of a function with a body - so that a crate's test build and
// a dependency's copy of it do not both define one - and `main` went with it.
#![no_main]

#[no_mangle]
fn main(_argc: isize, _argv: *const *const u8) -> isize {
    let words: Vec<&str> = "a b c".split(' ').collect();
    assert_eq!(words.len(), 3);
    0
}
