struct Data { first: u32, second: u32 }

fn main() {
    let Data {
        #[allow(unused_variables)]
        first,
        #[doc = "retained field"]
        second: kept,
    } = Data { first: 13, second: 29 };
    assert_eq!((first, kept), (13, 29));
}
