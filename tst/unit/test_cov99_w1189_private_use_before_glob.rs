mod source {
    pub struct Item;
}

mod relay {
    pub use crate::source::*;
    use crate::source::Item as PrivateItem;
}

use relay::*;

fn main() {
    let _ = Item;
}
