mod plain {
    pub const BASE: u16 = 17;
}

#[path = "virtual_root"]
mod rooted {
    #[path = "virtual_child"]
    pub mod nested {
        pub fn value(base: u16) -> u16 { base * 2 + 8 }
    }
}

fn main() {
    assert_eq!(rooted::nested::value(plain::BASE), 42);
}
