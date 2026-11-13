macro_rules! repeat {
    ($item:item) => {
        $item
        $item
    };
}

repeat!(#[cfg(any())] mod deliberately_absent_module;);

fn main() {}
