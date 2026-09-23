// async-trait is `#[async_trait] impl Trait for Struct { type Assoc = (); .. }`.
// An attribute macro is handed the whole item; our printer of an impl knew
// only its functions and statics, and left out the items' attributes.
//@ proc-macro-aux-build: proc_macro_item_passthrough.rs

use proc_macro_item_passthrough::echo_item_spelled;

trait Trait {
    type Assoc;
    fn get(&self) -> Self::Assoc;
}

struct Struct;

#[echo_item_spelled("type Assoc")]
#[echo_item_spelled("inline")]
impl Trait for Struct {
    type Assoc = u8;

    #[inline]
    fn get(&self) -> u8 {
        3
    }
}

fn main() {
    let value: <Struct as Trait>::Assoc = Struct.get();
    assert_eq!(value, 3);
}
