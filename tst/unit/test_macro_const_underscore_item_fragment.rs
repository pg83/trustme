macro_rules! duplicate {
    ($item:item) => {
        $item
        $item
    };
}

duplicate!(const _: () = (););

fn main() {}
