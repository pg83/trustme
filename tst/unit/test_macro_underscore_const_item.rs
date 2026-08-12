macro_rules! duplicate_item {
    ($item:item) => {
        $item
        $item
    };
}

duplicate_item!(const _: () = (););

fn main() {}
