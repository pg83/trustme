#[cfg(native_link_search)]
#[link(name = "native_link_search", kind = "static")]
extern "C" {
    fn native_link_search_value() -> i32;
}

fn main() {
    #[cfg(native_link_search)]
    unsafe {
        assert!(native_link_search_value() == 91);
    }
}
