struct Window<const N: usize>;

impl Window<{ 2 + 3 }> {
    fn local_name() -> &'static str {
        struct Marker;
        std::any::type_name::<Marker>()
    }
}

fn main() {
    let name = Window::<5>::local_name();
    assert!(name.ends_with("::local_name::Marker"));
}
