//@ aux-build: local_item_in_const_arg_impl.rs
// tor-hscrypto (arti) implements `ShortMac<HS_MAC_LEN>` for `HsMacKey<'a>`,
// and `validate` declares `use subtle::ConstantTimeEq;` in its body. Items in
// a function's blocks are named after the function, so the crate records the
// owner's path, `<HsMacKey as ShortMac<HS_MAC_LEN>>::validate`, with the
// trait's const argument as written - upstream's impl header is not
// normalized either. Writing the crate's metadata then failed on the
// argument's body, a named constant: "Unexpected Constant: HS_MAC_LEN".
use local_item_in_const_arg_impl::{Key, ShortMac};

fn main() {
    assert!(Key(&[1, 2]).validate(&[1, 2, 3, 4]));
    assert!(!Key(&[2]).validate(&[1, 2, 3, 4]));
}
