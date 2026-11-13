//@ compile-fail: Item of type Struct is not allowed in a trait

macro_rules! emit_trait {
    ($item:item) => {
        trait Invalid {
            $item
        }
    };
}

emit_trait! { struct NotAnAssociatedItem; }

fn main() {}
