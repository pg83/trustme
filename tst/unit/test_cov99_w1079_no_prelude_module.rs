#[no_prelude]
mod isolated {
    pub fn value() -> ::std::option::Option<u32> {
        ::std::option::Option::Some(1079)
    }
}

fn main() {
    assert_eq!(isolated::value(), ::std::option::Option::Some(1079));
}
