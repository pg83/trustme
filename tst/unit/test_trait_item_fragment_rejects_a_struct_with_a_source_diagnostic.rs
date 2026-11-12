//@ compile-fail: is not allowed in a trait

macro_rules! install_item {
    ($item:item) => {
        trait Container {
            $item
        }
    };
}

install_item!(struct Hidden;);

fn main() {}
