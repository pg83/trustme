macro_rules! copies {
    ($item:item) => {
        mod first { $item pub fn length() -> usize { ext::mem::size_of::<u32>() } }
        mod second { $item pub fn length() -> usize { ext::mem::size_of::<u64>() } }
    };
}

copies!(extern crate std as ext;);

fn main() {
    assert_eq!(first::length(), 4);
    assert_eq!(second::length(), 8);
}
