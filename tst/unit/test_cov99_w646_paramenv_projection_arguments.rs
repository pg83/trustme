//@ compile-fail: type annotations needed
//@ compile-flags: --emit=metadata

mod routes {
    pub trait Source {
        type Item;
    }

    pub trait Left {
        fn w646_route(&self, value: u8) -> u8;
    }

    pub trait Right {
        fn w646_route(&self, value: u16) -> u16;
    }
}

fn unresolved<T: routes::Left + routes::Right, U: routes::Source>(value: T, arg: U::Item) {
    value.w646_route(arg);
}

fn main() {}
