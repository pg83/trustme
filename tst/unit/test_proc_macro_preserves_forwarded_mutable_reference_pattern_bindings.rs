//@ proc-macro-aux-build: forward_mutable_pattern_tokens.rs
extern crate forward_mutable_pattern_tokens;

use forward_mutable_pattern_tokens::preserve;

macro_rules! make_update {
    ($binding:ident, $pattern:pat) => {
        preserve! {
            fn update(value: &mut Option<u32>) -> u32 {
                match *value {
                    $pattern => {
                        *$binding += 7;
                        *$binding
                    }
                    _ => 0,
                }
            }
        }
    };
}

make_update!(number, Some(ref mut number));

fn main() {
    let mut present = Some(37);
    assert_eq!(update(&mut present), 44);
    assert_eq!(present, Some(44));
    let mut absent = None;
    assert_eq!(update(&mut absent), 0);
    assert_eq!(absent, None);
}
