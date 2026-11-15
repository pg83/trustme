//@ compile-fail: Unknown associated item gone of trait
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(import_trait_associated_functions)]

trait Filtered {
    #[cfg(any())]
    fn gone();
}

use Filtered::gone;

fn main() {}
