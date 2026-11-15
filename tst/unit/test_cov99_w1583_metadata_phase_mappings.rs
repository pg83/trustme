//@ crate-type: lib
//@ compile-flags: --emit=metadata --env-set TRUSTME_DUMPMEM=1

#![no_std]

pub fn preserve_word(word: u64) -> u64 {
    word
}
