trait HasArray {
    type Array<const N: u8>;

    fn array(&self) -> Self::Array<3>;
}

fn main() {}
